#include "VDbg.h"
//typedef map<DWORD, std::wstring> MODULE_MAP
BOOL CALLBACK EnumerateLoadedModulesProc64(PCTSTR  ModuleName, DWORD64 ModuleBase, ULONG   ModuleSize,
										   PVOID   UserContext);
BOOL CALLBACK SymEnumSymbolsProc(PSYMBOL_INFO pSymInfo, ULONG SymbolSize, PVOID UserContext);
BOOL CALLBACK SymEnumSymbolsLocalProc(PSYMBOL_INFO pSymInfo, ULONG SymbolSize, PVOID UserContext);

BaseTypeEntry g_baseTypeNameMap[] = {
   { cbtNone, "<no-type>" },
   { cbtVoid, "void" },
   { cbtBool, "bool" },
   { cbtChar, "char" },
   { cbtUChar, "unsigned char"},
   { cbtWChar, "wchar_t" },
   { cbtShort, "short" },
   { cbtUShort, "unsigned short" },
   { cbtInt, "int" },
   { cbtUInt, "unsigned int" },
   { cbtLong, "long" },
   { cbtULong, "unsigned long" },
   { cbtLongLong, "long long" },
   { cbtULongLong, "unsigned long long" },
   { cbtFloat,"float" },
   { cbtDouble, "double" },
   { cbtEnd, "" },
};

/**
 * ���캯��
 * ��ʼ���ϵ��map, ����һ���¼�(��waitForSingleObject�����߳���)
 */
VDbg::VDbg()
{
	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);

	ZeroMemory(&pi, sizeof(pi));

	bpHitOnce = false;
	srcReadOnce = false;

	hHalt = CreateEvent(NULL, false, false, TEXT("haltExecution"));
	
	debug_mode = INITIAL_STATE;
}
/**
 * ��ʼ���ԣ��������� �Լ� ����debugger loop
 * @param path ��ִ���ļ���
 */
void VDbg::StartDebugging(LPCWSTR path)
{
	// C:\\projects\\debugme\\Debug\\DebugMe.exe
	// C:\\projects\\leetcode\\Debug\\leetcode.exe
    if (!CreateProcess(
		path,
        NULL,
        NULL,
        NULL,
        FALSE,
        DEBUG_ONLY_THIS_PROCESS || CREATE_SUSPENDED,
		//��Ȼsuspend�ˣ�������debugѭ��continue֮���ǻ��������,������Ҫ���waitForSingleObjectʹ��
        NULL,
        NULL,
        &si,
        &pi)) 
	{

        cout << "create process failed" <<GetLastError()<<endl;
        return;
    }
	bool wait = true;
	while(wait && WaitForDebugEvent(&debug_event, INFINITE))
	{
		switch (debug_event.dwDebugEventCode) 
		{
			case CREATE_PROCESS_DEBUG_EVENT:
				OnProcessCreated();
				break;

			case CREATE_THREAD_DEBUG_EVENT:
				//OnThreadCreated(&debugEvent.u.CreateThread);
				break;

			case EXCEPTION_DEBUG_EVENT:
				OnException();
				break;

			case EXIT_PROCESS_DEBUG_EVENT:
				//OnProcessExited(&debugEvent.u.ExitProcess);
				wait = FALSE;
				break;

			case EXIT_THREAD_DEBUG_EVENT:
				//OnThreadExited(&debugEvent.u.ExitThread);
				break;

			case LOAD_DLL_DEBUG_EVENT:
				//OnDllLoaded(&debugEvent.u.LoadDll);
				break;

			case UNLOAD_DLL_DEBUG_EVENT:
				//OnDllUnloaded(&debugEvent.u.UnloadDll);
				break;

			case OUTPUT_DEBUG_STRING_EVENT:
				OnOutputDebugString();
				break;

			case RIP_EVENT:
				//OnRipEvent(&debugEvent.u.RipInfo);
				break;

			default:
				cout<< "unknown events" << endl;
				break;
		}
		if (wait == TRUE)
		{
			ContinueDebugEvent(debug_event.dwProcessId, debug_event.dwThreadId, DBG_CONTINUE);
		}
		else
			break;
	}
}
/**
 * ��debuggee���̱�����ʱ����øú���
 * ��ȡ������ڵ���Ϣ������ڵ�����һ���ϵ�ͣ�����ȴ��û�����(��ʵԴ�뼶������Ӧ����main������һ�������Ķϵ�)
 */
void VDbg::OnProcessCreated()
{
	startAddress = (DWORD)debug_event.u.CreateProcessInfo.lpStartAddress;

	if(Symbol_Initialize())
	{
		cout << "done."<<endl;
	}
	else
	{
		cout << "symbols initialize failed" << endl;
	}
	ShowVdbg();
	WaitForSingleObject(hHalt,INFINITE);
}
/*
 * �������쳣ʱ���øú���
 * ���ݲ�ͬ���쳣�в�ͬ�Ĵ���
 */
void VDbg::OnException()
{
	BYTE  originalInstruction;
	DWORD dwWriteSize;
	switch(debug_event.u.Exception.ExceptionRecord.ExceptionCode)
	{

	 case EXCEPTION_BREAKPOINT:

		 if(bpHitOnce)
		 {
			// how do we know which break point to cancel? look at EIP (EIP - 1 actually)
			//cancelBreakPoint(startAddress);
			stop_addr = peekEIP() - 1;
			setTrap();
			setEIP(-1);
			originalInstruction = BPs[stop_addr].opcode;
			WriteDebuggeeMemory((LPVOID)stop_addr, &originalInstruction, 1);
			FlushInstructionCache(getDebuggeeProcessHandle(), (LPVOID)stop_addr, 1);
		  }
		  else
		  {
			 bpHitOnce = true; 
		  }
		  break;
	 case EXCEPTION_SINGLE_STEP:
			if(debug_mode == STEP_IN)
			{
				 IMAGEHLP_LINE64 * curr_line_info = GetLineInfoByAddr(peekEIP());
				 if(curr_line_info == NULL || (!strcmp(curr_line_info->FileName, stepin_line_info->FileName) && curr_line_info->LineNumber == stepin_line_info->LineNumber))
				 {
					setTrap();
					break;
				 }
				 else
				 {
					debug_mode = INITIAL_STATE;
				 }
			}
			else if(debug_mode == STEP_OVER)
			{
				 IMAGEHLP_LINE64 * curr_line_info = GetLineInfoByAddr(peekEIP());
				 if(curr_line_info == NULL || (!strcmp(curr_line_info->FileName, stepin_line_info->FileName) && curr_line_info->LineNumber == stepin_line_info->LineNumber))
				 {
					int len = isCurrentInstCall(peekEIP());
					if(len != 0)
					{
						if(!detectBreakPoint(peekEIP() + len))
							setBreakPoint(peekEIP() + len, true);
						break;
					}
					else
					{
						setTrap();
						break;
					}
				 }
				 else
				 {
					debug_mode = INITIAL_STATE;
				 }
			}
			else
			{
				if(BPs[stop_addr].invisible)
					originalInstruction = BPs[stop_addr].opcode;
				else
					originalInstruction = 0xCC;
				WriteDebuggeeMemory((LPVOID)stop_addr, &originalInstruction, 1);
				DWORD ret = peekEIP();
				FlushInstructionCache(getDebuggeeProcessHandle(), (LPVOID)stop_addr, 1);
			}
			if(!ShowSourceLine(peekEIP(), false))
				break; // if can not read file(because it's not the file user likes to debug!), run the program
			else
				ShowVdbg();
			WaitForSingleObject(hHalt,INFINITE);
			if(bSingleStepping())
				SingleStep();
		 break;
	}
}
/**
 * ���յ�debuggee������debug stringʱ����øú���
 * ���յ���debug string��ʾ��richTextBox
 */
void VDbg::OnOutputDebugString()
{
   OUTPUT_DEBUG_STRING_INFO & DebugString = debug_event.u.DebugString;
   char *msg = new char[DebugString.nDebugStringLength];
   ReadDebuggeeMemory(DebugString.lpDebugStringData, msg, DebugString.nDebugStringLength);
   cout<< "debugString: "<< msg << endl;
   delete []msg;
}

bool VDbg::Symbol_Initialize()
{
	cout << "Reading symbols ...";
	if (SymInitialize(getDebuggeeProcessHandle(), NULL, FALSE) == TRUE) {
		DWORD64 moduleAddress = SymLoadModule64(
			getDebuggeeProcessHandle(),
			debug_event.u.CreateProcessInfo.hFile, 
			NULL,
			"VDbgSymbol",
			(DWORD64)debug_event.u.CreateProcessInfo.lpBaseOfImage,
			0);

		if (moduleAddress == 0) {
			if(GetLastError() != 0)
			{
				cout << "unable to Load Module, err code: " << GetLastError() << endl;
				ShowVdbg();
				return false;
			}
			else
			{
				moduleAddress = (DWORD64)GetModuleHandle((LPCWSTR)"DebugMe");	
			}
		}
		IMAGEHLP_MODULE64 imghlp64;
		imghlp64.SizeOfStruct = sizeof(imghlp64);
		BOOL bSuccess = SymGetModuleInfo64(getDebuggeeProcessHandle(), moduleAddress, &imghlp64);
		if (!bSuccess || !(imghlp64.SymType == SymPdb))
		{
			cout << "unable to Get Module Info, err code: " << GetLastError() << endl;
			ShowVdbg();
			return false;
		}
		SymEnumSourceFiles(getDebuggeeProcessHandle(), moduleAddress,
                     "*.cpp", EnumSourceFilesProc, this);
		SymEnumSourceFiles(getDebuggeeProcessHandle(), moduleAddress,
                     "*.h", EnumSourceFilesProc, this);
		SymEnumSourceFiles(getDebuggeeProcessHandle(), moduleAddress,
                     "*.hpp", EnumSourceFilesProc, this);

	

	}
	else {
		return false;
	}
	return true;
}

/**
 * ���ݵ�ַ����һ���ϵ�
 * @param addr Ҫ���öϵ�ĵ�ַ
 */
void VDbg::setBreakPoint(DWORD addr, bool bInvisible)
{
	BREAK_POINT_INFO bp_info;
	BYTE cInstruction;
	DWORD dwReadBytes;

	ReadDebuggeeMemory((void*)addr, &cInstruction, 1);

	bp_info.opcode = cInstruction;
	bp_info.invisible = bInvisible;
	BPs[addr] = bp_info;
	cInstruction = 0xCC;
	WriteDebuggeeMemory((void*)addr, &cInstruction, 1);
	FlushInstructionCache(getDebuggeeProcessHandle(),(void*)addr, 1);
	DWORD ret = peekEIP();

}
/**
 * �ж�ĳ����ַ���Ƿ��Ѿ��жϵ�
 * @param addr Ҫ���öϵ�ĵ�ַ
 */
bool VDbg::detectBreakPoint(DWORD addr)
{
	BYTE cInstruction;
	ReadDebuggeeMemory((void*)addr, &cInstruction, 1);
	return cInstruction == 0xCC;
}
/**
 * ȡ���ϵ�
 * @param addr Ҫȡ���ϵ�ĵ�ַ
 */
void VDbg::cancelBreakPoint(DWORD addr, bool bInvisible)
{
	if(BPs.find(addr) == BPs.end())
	{
		cout << "no break point on this line ." << endl;
		ShowVdbg();
		return;
	}
	BYTE originalInstruction = BPs[addr].opcode;
	DWORD dwWriteSize;
	WriteDebuggeeMemory((LPVOID)addr, &originalInstruction, 1);
	FlushInstructionCache(getDebuggeeProcessHandle(), (LPVOID)addr, 1);
	BPs.erase(addr);

}
/**
 * �õ�ĳһ�����͵ı�����ֵ�����ַ�����ʽ����
 * @param tyoeID ����������
 * @param modBase ģ���׵�ַ, symbol_info�ṹ�и���
 * @param pData ������ŵĵ�ַ
 */
string VDbg::GetTypeValue(int typeID, DWORD modBase, BYTE* pData)
{
    DWORD typeTag;
    SymGetTypeInfo(
        getDebuggeeProcessHandle(),
        modBase,
        typeID,
        TI_GET_SYMTAG,
        &typeTag);

    switch (typeTag) {
        
        case SymTagBaseType:
            return GetBaseTypeValue(typeID, modBase, pData);

        case SymTagPointerType:
            return GetPointerTypeValue(typeID, modBase, pData);

        case SymTagEnum:
            return GetEnumTypeValue(typeID, modBase, pData);

        case SymTagArrayType:
            return GetArrayTypeValue(typeID, modBase, pData);

        case SymTagUDT:
            return GetUDTTypeValue(typeID, modBase, pData);

        case SymTagTypedef:

            //��ȡ�������͵�ID
            DWORD actTypeID;
            SymGetTypeInfo(
                getDebuggeeProcessHandle(),
                modBase,
                typeID,
                TI_GET_TYPEID,
                &actTypeID);

            return GetTypeValue(actTypeID, modBase, pData);

        default:
            return "??";
    }
}
/**
 * �õ�ĳһ���������͵ı�����ֵ�����ַ�����ʽ����
 * @param tyoeID ����������
 * @param modBase ģ���׵�ַ, symbol_info�ṹ�и���
 * @param pData ������ŵĵ�ַ
 */
string	VDbg::GetBaseTypeValue(int typeID, DWORD modBase, BYTE * pData)
{
	CBaseTypeEnum baseType = GetBaseTypeEnumID(typeID, modBase);
	ostringstream valueBuilder;
	pData = CopyDebuggeeMemoryToDebugger(pData, 8); // Base Type is at most 8 byte
	switch (baseType) {

		case cbtNone:
			valueBuilder << "??";
			break;

		case cbtVoid:
			valueBuilder << "??";
			break;

		case cbtBool:
			valueBuilder << (*pData == 0 ? L"false" : L"true");
			break;

		case cbtChar:
			valueBuilder << ConvertToSafeChar(*((char*)pData));
			break;

		case cbtUChar:
			valueBuilder << std::hex 
						 << std::uppercase 
						 << std::setw(2) 
						 << std::setfill('0') 
						 << *((unsigned char*)pData);
			break;

		case cbtWChar:
			valueBuilder << ConvertToSafeWChar(*((wchar_t*)pData));
			break;

		case cbtShort:
			valueBuilder << *((short*)pData);
			break;

		case cbtUShort:
			valueBuilder << *((unsigned short*)pData);
			break;

		case cbtInt:
			valueBuilder << *((int*)pData);

			break;

		case cbtUInt:
			valueBuilder << *((unsigned int*)pData);
			break;

		case cbtLong:
			valueBuilder << *((long*)pData);
			break;

		case cbtULong:
			valueBuilder << *((unsigned long*)pData);
			break;

		case cbtLongLong:
			valueBuilder << *((long long*)pData);
			break;

		case cbtULongLong:
			valueBuilder << *((unsigned long long*)pData);
			break;

		case cbtFloat:
			valueBuilder << *((float*)pData);
			break;

		case cbtDouble:
			valueBuilder << *((double*)pData);
			break;
	}
	string ss = valueBuilder.str();
	return valueBuilder.str();
}
/**
 * �õ�ĳһ��ָ�����͵ı�����ֵ�����ַ�����ʽ����
 * @param tyoeID ����������
 * @param modBase ģ���׵�ַ, symbol_info�ṹ�и���
 * @param pData ������ŵĵ�ַ
 */
string VDbg::GetPointerTypeValue(int typeID, DWORD modBase, BYTE* pData)
{
	ostringstream valueBuilder;
	pData = CopyDebuggeeMemoryToDebugger(pData, 4);
	valueBuilder << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << *((DWORD*)pData);
	return valueBuilder.str();
}
/**
 * �õ�ĳһ���������͵ı�����ֵ�����ַ�����ʽ����
 * @param tyoeID ����������
 * @param modBase ģ���׵�ַ, symbol_info�ṹ�и���
 * @param pData ������ŵĵ�ַ
 */
string VDbg:: GetArrayTypeValue(int typeID, DWORD modBase, BYTE* pData) {


	DWORD elemCount;
	SymGetTypeInfo(
		getDebuggeeProcessHandle(),
		modBase,
		typeID,
		TI_GET_COUNT,
		&elemCount);

	elemCount = elemCount > 32 ? 32 : elemCount;

	DWORD innerTypeID;
	SymGetTypeInfo(
		getDebuggeeProcessHandle(),
		modBase,
		typeID,
		TI_GET_TYPEID,
		&innerTypeID);


	ULONG64 elemLen;
	SymGetTypeInfo(
		getDebuggeeProcessHandle(),
		modBase,
		innerTypeID,
		TI_GET_LENGTH,
		&elemLen);

	ostringstream valueBuilder;

	for (int index = 0; index != elemCount; ++index) {

		DWORD elemOffset = DWORD(index * elemLen);

		valueBuilder << "[" << index << "]: "
					 << GetTypeValue(innerTypeID, modBase, pData + index * elemLen);

		if (index != elemCount - 1) {
			valueBuilder << std::endl;
		}
	}

	return valueBuilder.str();
}
/**
 * �õ�ĳһ��ö�����͵ı�����ֵ�����ַ�����ʽ����
 * @param tyoeID ����������
 * @param modBase ģ���׵�ַ, symbol_info�ṹ�и���
 * @param pData ������ŵĵ�ַ
 */
string VDbg::GetEnumTypeValue(int typeID, DWORD modBase,  BYTE* pData) {

	string valueName;


	CBaseTypeEnum cBaseType = GetBaseTypeEnumID(typeID, modBase);


	DWORD childrenCount;
	SymGetTypeInfo(
		getDebuggeeProcessHandle(),
		modBase,
		typeID,
		TI_GET_CHILDRENCOUNT,
		&childrenCount);


	TI_FINDCHILDREN_PARAMS* pFindParams =
		(TI_FINDCHILDREN_PARAMS*)malloc(sizeof(TI_FINDCHILDREN_PARAMS) + childrenCount * sizeof(ULONG));
	pFindParams->Start = 0;
	pFindParams->Count = childrenCount;

	SymGetTypeInfo(
		getDebuggeeProcessHandle(),
		modBase,
		typeID,
		TI_FINDCHILDREN,
		pFindParams);

	for (int index = 0; index != childrenCount; ++index) {


		VARIANT enumValue;
		SymGetTypeInfo(
			getDebuggeeProcessHandle(),
			modBase,
			pFindParams->ChildId[index],
			TI_GET_VALUE,
			&enumValue);

		if (VariantEqual(enumValue, cBaseType, pData) == TRUE) {


			char * pBuffer;
			SymGetTypeInfo(
				getDebuggeeProcessHandle(),
				modBase,
				pFindParams->ChildId[index],
				TI_GET_SYMNAME,
				&pBuffer);

			valueName = pBuffer;
			LocalFree(pBuffer);

			break;
		}
	}

	free(pFindParams);


	if (valueName.length() == 0) {

		valueName = GetBaseTypeValue(typeID, modBase, pData);
	}

	return valueName;
}
/**
 * �õ��Զ������͵ı�����ֵ�����ַ�����ʽ����
 * @param tyoeID ����������
 * @param modBase ģ���׵�ַ, symbol_info�ṹ�и���
 * @param pData ������ŵĵ�ַ
 */
string VDbg::GetUDTTypeValue(int typeID, DWORD modBase, BYTE* pData) {

	ostringstream valueBuilder;


	DWORD memberCount;
	SymGetTypeInfo(
		getDebuggeeProcessHandle(),
		modBase,
		typeID,
		TI_GET_CHILDRENCOUNT,
		&memberCount);


	TI_FINDCHILDREN_PARAMS* pFindParams =
		(TI_FINDCHILDREN_PARAMS*)malloc(sizeof(TI_FINDCHILDREN_PARAMS) + memberCount * sizeof(ULONG));
	pFindParams->Start = 0;
	pFindParams->Count = memberCount;

	SymGetTypeInfo(
		getDebuggeeProcessHandle(),
		modBase,
		typeID,
		TI_FINDCHILDREN,
		pFindParams);


	for (int index = 0; index != memberCount; ++index) {

		BOOL isDataMember = GetDataMemberInfo(
			pFindParams->ChildId[index],
			modBase,
			pData,
			valueBuilder);

		if (isDataMember == TRUE) {
			valueBuilder << std::endl;
		}
	}

	valueBuilder.seekp(-1, valueBuilder.end);
	valueBuilder.put(0);

	return valueBuilder.str();
}
/**
 * �õ�ĳһ���Զ������͵�ĳ����Ա�����Ϣ�����ַ�����ʽ����
 * @param memberID ��ԱID
 * @param modBase ģ���׵�ַ, symbol_info�ṹ�и���
 * @param pData ������ŵĵ�ַ
 * @param valueBuilder �����
 */
BOOL VDbg::GetDataMemberInfo(DWORD memberID, DWORD modBase, BYTE* pData, ostringstream & valueBuilder)
{

	DWORD memberTag;
	SymGetTypeInfo(
		getDebuggeeProcessHandle(),
		modBase,
		memberID,
		TI_GET_SYMTAG,
		&memberTag);

	if (memberTag != SymTagData && memberTag != SymTagBaseClass) {
		return FALSE;
	}

	valueBuilder << "";

	DWORD memberTypeID;
	SymGetTypeInfo(
		getDebuggeeProcessHandle(),
		modBase,
		memberID,
		TI_GET_TYPEID,
		&memberTypeID);


	valueBuilder << GetTypeName(memberTypeID, modBase);


	if (memberTag == SymTagData) {

		char * name;
		SymGetTypeInfo(
			getDebuggeeProcessHandle(),
			modBase,
			memberID,
			TI_GET_SYMNAME,
			&name);

		valueBuilder << "  " << name;

		LocalFree(name);
	}
	else {

		valueBuilder << "  <base-class>";
	}


	ULONG64 length;
	SymGetTypeInfo(
		getDebuggeeProcessHandle(),
		modBase,
		memberTypeID,
		TI_GET_LENGTH,
		&length);

	valueBuilder << "  " << length;


	DWORD offset;
	SymGetTypeInfo(
		getDebuggeeProcessHandle(),
		modBase,
		memberID,
		TI_GET_OFFSET,
		&offset);

	DWORD childAddress = (DWORD)pData + offset;

	valueBuilder << "  " << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << childAddress << std::dec;


	if (IsSimpleType(memberTypeID, modBase) == TRUE) {

		valueBuilder << "  " 
						<< GetTypeValue(
							memberTypeID,
							modBase,
							(BYTE *)childAddress
							);
	}

	return TRUE;
}
/**
 * �õ�typeID��Ӧ�����ͣ����ַ�����ʽ����
 * @param tyoeID ����������
 * @param modBase ģ���׵�ַ, symbol_info�ṹ�и���
 */
string VDbg::GetTypeName(int typeID, DWORD modBase)
{

	DWORD typeTag;
	SymGetTypeInfo(
		 getDebuggeeProcessHandle(),
		modBase,
		typeID,
		TI_GET_SYMTAG,
		&typeTag);

	switch (typeTag) {
		
		case SymTagBaseType:
			return GetBaseTypeName(typeID, modBase);

		case SymTagPointerType:
			return GetPointerTypeName(typeID, modBase);

		case SymTagArrayType:
			return GetArrayTypeName(typeID, modBase);

		case SymTagUDT:
			return GetUDTTypeName(typeID, modBase);

		case SymTagEnum:
			return GetEnumTypeName(typeID, modBase);

		case SymTagFunctionType:
			return GetFunctionTypeName(typeID, modBase);

		default:
			return "??";
	}
}
/**
 * �õ���������typeID��Ӧ�����ͣ����ַ�����ʽ����
 * @param tyoeID ����������
 * @param modBase ģ���׵�ַ, symbol_info�ṹ�и���
 */
string VDbg::GetBaseTypeName(int typeID, DWORD modBase)
{

	CBaseTypeEnum baseType = GetBaseTypeEnumID(typeID, modBase);

	int index = 0;

	while (g_baseTypeNameMap[index].type != cbtEnd) {

		if (g_baseTypeNameMap[index].type == baseType) {
			break;
		}

		++index;
	}

	return g_baseTypeNameMap[index].name;
}
/**
 * �õ�ָ������typeID��Ӧ�����ͣ����ַ�����ʽ����
 * @param tyoeID ����������
 * @param modBase ģ���׵�ַ, symbol_info�ṹ�и���
 */
string VDbg::GetPointerTypeName(int typeID, DWORD modBase)
{
	BOOL isReference;
	SymGetTypeInfo(
		getDebuggeeProcessHandle(),
		modBase,
		typeID,
		TI_GET_IS_REFERENCE,
		&isReference);


	DWORD innerTypeID;
	SymGetTypeInfo(
		getDebuggeeProcessHandle(),
		modBase,
		typeID,
		TI_GET_TYPEID,
		&innerTypeID);

	return GetTypeName(innerTypeID, modBase) + (isReference == TRUE ? "&" : "*");
}
/**
 * �õ���������typeID��Ӧ�����ͣ����ַ�����ʽ����
 * @param tyoeID ����������
 * @param modBase ģ���׵�ַ, symbol_info�ṹ�и���
 */
string VDbg::GetArrayTypeName(int typeID, DWORD modBase)
{
	DWORD innerTypeID;
	SymGetTypeInfo(
		getDebuggeeProcessHandle(),
		modBase,
		typeID,
		TI_GET_TYPEID,
		&innerTypeID);

	DWORD elemCount;
	SymGetTypeInfo(
		getDebuggeeProcessHandle(),
		modBase,
		typeID,
		TI_GET_COUNT,
		&elemCount);

	stringstream strBuilder;

	strBuilder << GetTypeName(innerTypeID, modBase) << '[' << elemCount << ']';

	return strBuilder.str();
}
/**
 * �õ�ö������typeID��Ӧ�����ͣ����ַ�����ʽ����
 * @param tyoeID ����������
 * @param modBase ģ���׵�ַ, symbol_info�ṹ�и���
 */
string VDbg::GetEnumTypeName(int typeID, DWORD modBase)
{
	return GetNameableTypeName(typeID, modBase);
}
/**
 * �õ��Զ�������typeID��Ӧ�����ͣ����ַ�����ʽ����
 * @param tyoeID ����������
 * @param modBase ģ���׵�ַ, symbol_info�ṹ�и���
 */
string VDbg::GetUDTTypeName(int typeID, DWORD modbase)
{
	return GetNameableTypeName(typeID, modbase);
}
/**
 * �õ��Զ������ֵ�����typeID��Ӧ�����ͣ����ַ�����ʽ����
 * @param tyoeID ����������
 * @param modBase ģ���׵�ַ, symbol_info�ṹ�и���
 */
string VDbg::GetNameableTypeName(int typeID, DWORD modBase)
{

	char * pBuffer;
	SymGetTypeInfo(
		getDebuggeeProcessHandle(),
		modBase,
		typeID,
		TI_GET_SYMNAME,
		&pBuffer);

	string typeName(pBuffer);

	LocalFree(pBuffer);

	return typeName;
}
/**
 * �õ���������typeID��Ӧ�����ͣ����ַ�����ʽ����
 * @param tyoeID ����������
 * @param modBase ģ���׵�ַ, symbol_info�ṹ�и���
 */
string VDbg::GetFunctionTypeName(int typeID, DWORD modBase)
{
	ostringstream nameBuilder;

	DWORD paramCount;
	SymGetTypeInfo(
		getDebuggeeProcessHandle(),
		modBase,
		typeID,
		TI_GET_CHILDRENCOUNT,
		&paramCount);


	DWORD returnTypeID;
	SymGetTypeInfo(
		getDebuggeeProcessHandle(),
		modBase,
		typeID,
		TI_GET_TYPEID,
		&returnTypeID);

	nameBuilder << GetTypeName(returnTypeID, modBase);


	BYTE* pBuffer = (BYTE*)malloc(sizeof(TI_FINDCHILDREN_PARAMS) + sizeof(ULONG) * paramCount);
	TI_FINDCHILDREN_PARAMS* pFindParams = (TI_FINDCHILDREN_PARAMS*)pBuffer;
	pFindParams->Count = paramCount;
	pFindParams->Start = 0;

	SymGetTypeInfo(
		getDebuggeeProcessHandle(),
		modBase,
		typeID,
		TI_FINDCHILDREN,
		pFindParams);

	nameBuilder << '(';

	for (int index = 0; index != paramCount; ++index) {

		DWORD paramTypeID;
		SymGetTypeInfo(
			getDebuggeeProcessHandle(),
			modBase,
			pFindParams->ChildId[index],
			TI_GET_TYPEID,
			&paramTypeID);

		if (index != 0) {
			nameBuilder << ", ";
		}

		nameBuilder << GetTypeName(paramTypeID, modBase);
	}

	nameBuilder << ')';

	free(pBuffer);

	return nameBuilder.str();
}

/**
 * �Ƿ�Ϊ������ָ��ȼ򵥵�����
 * @param tyoeID ����������
 * @param modBase ģ���׵�ַ, symbol_info�ṹ�и���
 */
BOOL VDbg::IsSimpleType(DWORD typeID, DWORD modBase)
{

	DWORD symTag;
	SymGetTypeInfo(
		getDebuggeeProcessHandle(),
		modBase,
		typeID,
		TI_GET_SYMTAG,
		&symTag);

	switch (symTag) {

		case SymTagBaseType:
		case SymTagPointerType:
		case SymTagEnum:
			return TRUE;

		default:
			return FALSE;
	}
}
/**
 * ���������Ƿ����
 */
bool VDbg::VariantEqual(VARIANT var, CBaseTypeEnum cBaseType, BYTE* pData) {

	switch (cBaseType) {

		case cbtChar:
			return var.cVal == *((char*)pData);

		case cbtUChar:
			return var.bVal == *((unsigned char*)pData);

		case cbtShort:
			return var.iVal == *((short*)pData);

		case cbtWChar:
		case cbtUShort:
			return var.uiVal == *((unsigned short*)pData);

		case cbtUInt:
			return var.uintVal == *((int*)pData);

		case cbtLong:
			return var.lVal == *((long*)pData);

		case cbtULong:
			return var.ulVal == *((unsigned long*)pData);

		case cbtLongLong:
			return var.llVal == *((long long*)pData);

		case cbtULongLong:
			return var.ullVal == *((unsigned long long*)pData);

		case cbtInt:
		default:
			return var.intVal == *((int*)pData);
	}
}
/**
 * �õ�ö������typeID��Ӧ�����ͣ����ַ�����ʽ����
 * @param tyoeID ����������
 * @param modBase ģ���׵�ַ, symbol_info�ṹ�и���
 */
CBaseTypeEnum VDbg::GetBaseTypeEnumID(int typeID, DWORD modBase)
{

	//��ȡBaseTypeEnum
	DWORD baseType;
	ULONG64 length;
	SymGetTypeInfo(
		getDebuggeeProcessHandle(),
		modBase,
		typeID,
		TI_GET_BASETYPE,
		&baseType);

	//��ȡ�������͵ĳ���
	SymGetTypeInfo(
		getDebuggeeProcessHandle(),
		modBase,
		typeID,
		TI_GET_LENGTH,
		&length);

	switch (baseType) {
		case btVoid:
			return cbtVoid;

		case btChar:
			return cbtChar;

		case btWChar:
			return cbtWChar;

		case btInt:
			switch (length) {
				case 2:  return cbtShort;
				case 4:  return cbtInt;
				default: return cbtLongLong;
			}

		case btUInt:
			switch (length) {
				case 1:  return cbtUChar;
				case 2:  return cbtUShort;
				case 4:  return cbtUInt;
				default: return cbtULongLong;
			}

		case btFloat:
			switch (length) {
				case 4:  return cbtFloat;
				default: return cbtDouble;
			}

		case btBool:
			return cbtBool;

		case btLong:
			return cbtLong;

		case btULong:
			return cbtULong;

		default:
			return cbtNone;
	}
}
/**
 * �ѱ����Գ����ڴ��е����ݿ��������������
 * @param pDta ��ַ
 * @param Size ��С
 */
BYTE *  VDbg::CopyDebuggeeMemoryToDebugger(BYTE * pData, DWORD Size)
{
	BYTE * temp_ptr = (BYTE*) malloc(Size);
	ReadDebuggeeMemory(pData, temp_ptr, Size);
	return temp_ptr;
}
/**
 * ��ӡ��ǰ���з�һ���ԵĶϵ�
 */
void VDbg::Print_BP_INFO()
{
	for(auto it = BPs.begin(); it != BPs.end(); it++)
	{
		if(!it->second.invisible)
		{
			cout << "Address: ";
			PrintHex(it->first, 8, true);
			cout << " ";
			ShowSourceLine(it->first, true);
			cout<<endl;
		}
	}
	ShowVdbg();
}
/**
 * ��ӡĳ��������ֵ
 * @param var ������
 */
void VDbg::Print_Var(string var)
{
	CONTEXT lcContext;
	DWORD var_addr;
	DWORD valueOfSymbol = 0;
	int   nSize = 0;
	string str = "";
	GetDebuggeeContext(&lcContext);
	IMAGEHLP_STACK_FRAME imghlpstack;
	imghlpstack.InstructionOffset = peekEIP();

	SymEnumSymbols(pi.hProcess, SymGetModuleBase64(pi.hProcess, peekEIP()), (PCSTR)"", SymEnumSymbolsProc, &symbol_map);

	if (symbol_map.find(var) != symbol_map.end())
	{
		var_addr = symbol_map[var].Address;
		str  = GetTypeValue(symbol_map[var].type, symbol_map[var].modBase, (BYTE*)var_addr);
	}
	else //
	{
		SymSetContext(getDebuggeeProcessHandle(), &imghlpstack, &lcContext);
		SymEnumSymbols(pi.hProcess, 0, (PCSTR)"", SymEnumSymbolsLocalProc, &local_symbol_map);
		if(local_symbol_map.find(var) != local_symbol_map.end())
			var_addr = local_symbol_map[var].Address + lcContext.Ebp;
		else
			return;
		str  = GetTypeValue(local_symbol_map[var].type, local_symbol_map[var].modBase, (BYTE *)var_addr);
	}
	
	cout << str << endl;
	ShowVdbg();
}
/**
 * ��ӡ�ڴ�ĳ�ڴ��ַ��������
 * @param Address ��ַ
 * @param nSize ��ӡ����
 */
void VDbg::Print_Mem(DWORD Address, DWORD nSize)
{
	vector<char> ASCII;
	for(DWORD i = 1; i <= nSize; i++)
	{
		BYTE read;
		char c;
		ReadDebuggeeMemory((LPCVOID)(Address + (i - 1)), &read, 1);
		PrintHex(read, 2, false);
		if(read < 32 || read > 127)
			c = '.';
		else
			c = (char)read;
		ASCII.push_back(c);
		cout << " ";
		if(i % 16 == 0) 
		{
			for(int i = 0; i < ASCII.size(); i++)
				cout << ASCII[i];
			ASCII.clear();
			cout << endl;
		}
	}
	while(nSize % 16 != 0)
	{
		nSize++;
		cout << "   ";
	}
	if(ASCII.size() != 0)
		for(int i = 0; i < ASCII.size(); i++)
				cout << ASCII[i];
	cout << endl;
	ShowVdbg();
}
/**
 * ��ӡ��ǰ��Χ�����оֲ�����
 */
void VDbg::Print_LocalVars()
{
	CONTEXT lcContext;
	DWORD var_addr;
	DWORD valueOfSymbol = 0;
	int   nSize = 0;
	GetDebuggeeContext(&lcContext);
	IMAGEHLP_STACK_FRAME imghlpstack;
	imghlpstack.InstructionOffset = peekEIP();

	SymSetContext(getDebuggeeProcessHandle(), &imghlpstack, &lcContext);
	SymEnumSymbols(pi.hProcess, 0, (PCSTR)"", SymEnumSymbolsLocalProc, &local_symbol_map);
	for(auto it = local_symbol_map.begin(); it != local_symbol_map.end(); it++)
	{
		var_addr =  it->second.Address + lcContext.Ebp;
		string str  = GetTypeValue(it->second.type, it->second.modBase, (BYTE *)var_addr);
		cout << it->second.name << ": "<< endl <<str << endl;
	}
	ShowVdbg();
}
/**
 * ��ӡ����ȫ�ֱ���
 */
void VDbg::Print_GlobalVars()
{
	CONTEXT lcContext;
	DWORD var_addr;
	DWORD valueOfSymbol = 0;
	int   nSize = 0;
	GetDebuggeeContext(&lcContext);
	IMAGEHLP_STACK_FRAME imghlpstack;
	imghlpstack.InstructionOffset = peekEIP();
	SymEnumSymbols(pi.hProcess, SymGetModuleBase64(pi.hProcess, peekEIP()), (PCSTR)"", SymEnumSymbolsProc, &symbol_map);
	for(auto it = symbol_map.begin(); it != symbol_map.end(); it++)
	{
		var_addr =  it->second.Address;
		string str  = GetTypeValue(it->second.type, it->second.modBase, (BYTE *)var_addr);
		cout << it->second.name << ": "<< endl <<str << endl;
	}
	ShowVdbg();
}
/**
 * �����ļ������к����öϵ�
 * @param szFileName �ļ���
 * @param lineNumber �к�
 * @param bInvisible true��ʾΪһ���Զϵ㣬�����ǡ�
 */
void VDbg::SetBreakPointBySourceLine(char * szFileName, int lineNumber, bool bInvisible)
{
	DWORD addr = getAddrBySourceLine(szFileName, lineNumber);
	if(!detectBreakPoint(addr))
		setBreakPoint(addr, bInvisible);
	ShowVdbg();
}
/**
 * �����ļ������к�ȡ���ϵ�
 * @param szFileName �ļ���
 * @param lineNumber �к�
 * @param bInvisible true��ʾΪһ���Զϵ㣬�����ǡ�
 */
void VDbg::CancelBreakPointBySourceLine(char * szFileName, int lineNumber, bool bInvisible)
{
	cancelBreakPoint(getAddrBySourceLine(szFileName, lineNumber), bInvisible);
	ShowVdbg();
}
/**
 * �����ļ������к�ȡ���ϵ�
 * @param strFile �ѿ�ִ���ļ��õ���Դ�����ļ���ӵ�һ��vector��
 */
void VDbg::addSourceFiles(string strFile)
{
	enum_files.push_back(strFile);
}
/**
 * �õ���ִ���ļ��õ���Դ�����ļ�������
 */
int  VDbg::getSourceFilesNumber()
{
	return enum_files.size();
}
/**
 * ����һ���ļ������������ļ�����ȫ·��
 * @param path
 */
string VDbg::getFullPath(string path)
{
	for(int i = 0; i < enum_files.size(); i++)
	{
		int s = find_first_occurence(enum_files[i], path);
		if(s != -1)
			return enum_files[i];
	}
	return "";
}


/**
 * ���ݵ�ַ��ʾ��Ӧ��
 * @param addr ��ַ
 */
bool VDbg::ShowSourceLine(DWORD addr, bool bRaw)
{
	IMAGEHLP_LINE64 * lineInfo = GetLineInfoByAddr(addr);
	if(lineInfo == NULL)
	{
		cout << "fail to read lineInfo from address err code: " << GetLastError()<<endl;
		ShowVdbg();
		return false;
	}
	string t_file_name(lineInfo->FileName);
	if(source_file_map.find(t_file_name) == source_file_map.end())
	{
		ifstream source_temp(lineInfo->FileName);
		// ����޷���ȡ������ʾ��Դ�ļ��������ⲻ���û���Ҫ���Ե��ļ�����debugger run����
		if(source_temp.fail())
		{
			return false;
		}
		string temp;
		vector<string> * temp_vec = new vector<string>();
		while(getline(source_temp, temp))
		{
			temp_vec->push_back(temp);
		}
		source_file_map[lineInfo->FileName] = *temp_vec;
	}
	if(bRaw)
		cout<< strTrim((source_file_map[lineInfo->FileName])[lineInfo->LineNumber - 1]);
	else
		cout<< "=>" <<"\t " << (source_file_map[lineInfo->FileName])[lineInfo->LineNumber - 1] << endl;

	return true;
}
/**
 * �����кŵó��ڴ��ַ
 * @param szFileName �ļ���
 * @param lineNumber �к�
 */
DWORD VDbg::getAddrBySourceLine(char * szFileName, int lineNumber)
{
	IMAGEHLP_LINE64 imghlp_line64;
	long displacement;
	BOOL bSuccess = SymGetLineFromName64(getDebuggeeProcessHandle(), "VDbgSymbol", szFileName, lineNumber,
										 &displacement,
										 &imghlp_line64);
	return imghlp_line64.Address;
}
/**
 * ���ݵ�ַ�õ���Ӧ���е���Ϣ
 * @param addr ��ַ
 */
IMAGEHLP_LINE64 * VDbg::GetLineInfoByAddr(DWORD addr)
{
	IMAGEHLP_LINE64 * lineInfo = new IMAGEHLP_LINE64();

	lineInfo->SizeOfStruct = sizeof(lineInfo);
	DWORD displacement = 0;
	char buf[300];
	ZeroMemory(buf, 300 * sizeof(char));
	if (SymGetLineFromAddr64(getDebuggeeProcessHandle(), addr,
							&displacement,
							lineInfo) == FALSE) 
	{
		//��ʱ���޷���ȡ�ļ����к���Ϣ����Ϊ����ط��Ǳ��������ɵĶ���ĺ�����ת��Ϣ���������������������
		//�����и����ڵĵ��Է��Ź����������������Ǻ����������������޷��õ���Ϣ�ͼ�����������
		//cout << "failed err: " << GetLastError()<<endl;
		return NULL;
	}
	return lineInfo;
}
/**
 * ���õ�ǰ�ĵ���ģʽ�������ǵ������Եȵ�
 * @param mode
 */
void VDbg::setDebugMode(int mode)
{
	debug_mode = mode;
}
/**
 * ��ǰ�Ƿ����ڵ���������ߵ�������������
 */
bool VDbg::bSingleStepping()
{
	return debug_mode == STEP_IN || debug_mode == STEP_OVER;
}
/**
 * ���е�����׼������
 */
void VDbg::SingleStep()
{
	setTrap();
	stepin_line_info = GetLineInfoByAddr(peekEIP());
}
/**
 * ���е�����׼��������Ȼ��ѳ���������
 */
void VDbg::stepOut()
{
	DWORD retAddr = getReturnAddress();
	if(!detectBreakPoint(retAddr))
		setBreakPoint(retAddr, true);
	dbg->dbgSetEvent();
}
/**
 * ĳ��ַ���Ƿ���һ��callָ��
 */
int VDbg::isCurrentInstCall(DWORD addr)
{
	BYTE instruction[10];
	SIZE_T dwReadBytes = 0;
	ReadDebuggeeMemory((LPVOID)addr,instruction, 10);

	switch (instruction[0]) {

		case 0xE8:
			return 5;

		case 0x9A:
			return 7;

		case 0xFF:
			switch (instruction[1]) {

				case 0x10:
				case 0x11:
				case 0x12:
				case 0x13:
				case 0x16:
				case 0x17:
				case 0xD0:
				case 0xD1:
				case 0xD2:
				case 0xD3:
				case 0xD4:
				case 0xD5:
				case 0xD6:
				case 0xD7:
					return 2;

				case 0x14:
				case 0x50:
				case 0x51:
				case 0x52:
				case 0x53:
				case 0x54:
				case 0x55:
				case 0x56:
				case 0x57:
					return 3;

				case 0x15:
				case 0x90:
				case 0x91:
				case 0x92:
				case 0x93:
				case 0x95:
				case 0x96:
				case 0x97:
					return 6;

				case 0x94:
					return 7;
			}

		default:
			return 0;
	}
}
/**
 * ��ʾ��������ջ
 */
void VDbg::showCallStack()
{
	MODULE_MAP moduleMap;

	CONTEXT context;
	GetDebuggeeContext(&context);

	STACKFRAME64 stackFrame = { 0 };
	stackFrame.AddrPC.Mode = AddrModeFlat;
	stackFrame.AddrPC.Offset = context.Eip;
	stackFrame.AddrStack.Mode = AddrModeFlat;
	stackFrame.AddrStack.Offset = context.Esp;
	stackFrame.AddrFrame.Mode = AddrModeFlat;
	stackFrame.AddrFrame.Offset = context.Ebp;

	while (true) {

		//��ȡջ֡
		if (StackWalk64(
			IMAGE_FILE_MACHINE_I386,
			getDebuggeeProcessHandle(),
			getDebuggeeThreadHandle(),
			&stackFrame,
			&context,
			NULL,
			SymFunctionTableAccess64,
			SymGetModuleBase64,
			NULL) == FALSE) {

				break;
		}

		PrintHex((unsigned int)stackFrame.AddrPC.Offset, 8, true);
		cout << " ";

		BYTE buffer[sizeof(SYMBOL_INFO) + 128 * sizeof(TCHAR)] = { 0 };
		PSYMBOL_INFO pSymInfo = (PSYMBOL_INFO)buffer;
		pSymInfo->SizeOfStruct = sizeof(SYMBOL_INFO);
		pSymInfo->MaxNameLen = 128;
		
		DWORD64 displacement;

		if (SymFromAddr(
			getDebuggeeProcessHandle(), 
			stackFrame.AddrPC.Offset,
			&displacement,
			pSymInfo) == TRUE) 
		{
			cout << pSymInfo->Name << endl;
		}
		else {

			cout << "????" << endl;
		}
	}
	ShowVdbg();
}
/**
 * �õ������Խ��̵ļĴ�����Ϣ
 */
void VDbg::GetDebuggeeContext(CONTEXT * context)
{
	context->ContextFlags = CONTEXT_ALL;
	GetThreadContext(getDebuggeeThreadHandle(), context);
}

/**
* �鿴��ǰProgram Counterָ��ĵ�ַ���������Ա�����ʱ�۲�debuggee��ִ��״��
*/
DWORD VDbg::peekEIP()
{
	CONTEXT lcContext;
	lcContext.ContextFlags = CONTEXT_ALL;
	GetThreadContext(getDebuggeeThreadHandle(), &lcContext);
	return lcContext.Eip; 
}
	
/**
 * resume��WaitForSingleObject������߳�
 */
void VDbg::dbgSetEvent()
{
	SetEvent(hHalt);
}
/**
 * �õ������Խ��̵Ľ��̾��
 */
HANDLE VDbg::getDebuggeeProcessHandle()
{
	return pi.hProcess;
}
/**
 * �õ������Խ��̵��߳̾��
 */
HANDLE VDbg::getDebuggeeThreadHandle()
{
	return pi.hThread;
}
/**
 * �������Խ��̵��ڴ��ַ��������
 */
BOOL VDbg::ReadDebuggeeMemory(LPCVOID source, LPVOID lpBuffer, SIZE_T nSize)
{
	SIZE_T dwReadBytes = 0;
	return ReadProcessMemory(getDebuggeeProcessHandle(), source, lpBuffer, nSize, & dwReadBytes);
}
/**
 * д�뱻���Խ��̵��ڴ��ַ��������
 */
BOOL VDbg::WriteDebuggeeMemory(LPVOID target, LPCVOID source, SIZE_T nSize)
{
	SIZE_T dwReadBytes = 0;
	return WriteProcessMemory(getDebuggeeProcessHandle(), target, source, nSize, &dwReadBytes);
}
/**
 * �������壬��һ��ָ��������쳣��
 */
void VDbg::setTrap()
{
	CONTEXT lcContext;
	GetDebuggeeContext(&lcContext);
	lcContext.EFlags |= 0x100;
	SetThreadContext(getDebuggeeThreadHandle(), &lcContext);
}
/**
 * ���õ�ǰeip��ֵ
 */
void VDbg::setEIP(int difference)
{
	CONTEXT lcContext;
	GetDebuggeeContext(&lcContext);
	lcContext.Eip = lcContext.Eip + difference;
	SetThreadContext(getDebuggeeThreadHandle(), &lcContext);
}
/**
 * �õ���ǰ��ջ����Ϣ
 */
STACKFRAME64 * VDbg::getCurrStackFrame()
{
	CONTEXT context;
	GetDebuggeeContext(&context);

	STACKFRAME64  * stackFrame = new STACKFRAME64();
	stackFrame->AddrPC.Mode = AddrModeFlat;
	stackFrame->AddrPC.Offset = context.Eip;
	stackFrame->AddrStack.Mode = AddrModeFlat;
	stackFrame->AddrStack.Offset = context.Esp;
	stackFrame->AddrFrame.Mode = AddrModeFlat;
	stackFrame->AddrFrame.Offset = context.Ebp;

	if (StackWalk64(IMAGE_FILE_MACHINE_I386, getDebuggeeProcessHandle(), getDebuggeeThreadHandle(), stackFrame,
				   &context,
				   NULL,
				   SymFunctionTableAccess64,
				   SymGetModuleBase64,
				   NULL) == FALSE) 
		return NULL;
	return stackFrame;
}
/**
 * �õ���ǰ�����ķ��ص�ַ
 */
DWORD  VDbg::getReturnAddress()
{
	return getCurrStackFrame()->AddrReturn.Offset;
}
/**
 * ��ӡ�������봦����ʾ
 */
void  VDbg::ShowVdbg()
{
	cout << "<vdbg> ";
}
/**
 * �ַ��������ҵ�src�ַ�����һ�γ���target�ĵط�
 */
int VDbg::find_first_occurence(string src, string target)
{
	int j;
	for(int i = 0; i < src.length(); i++)
	{
		j = 0;
		char a = src[i];
		char b = target[j];
		if(src[i] == target[j])
		{
			int k = i;
			while(k < src.length() && j < target.length() && src[k] == target[j])
			{
				k++;
				j++;
			}
			if(k - i == target.length())
				return i;
			else
				continue;
		}
	}
	return -1;
}
/**
 * ��value��ʮ����ֹ��ʽ��ӡ����
 * @param value ֵ
 * @param width ���
 * @param width �Ƿ���0xΪǰ׺
 */
void VDbg::PrintHex(unsigned int value, int width, bool bHexPrefix)
{
	string prefix = (bHexPrefix) ? "0x" : "";
	cout << prefix << hex << uppercase << setw(width) << setfill('0') << value << dec << nouppercase << flush;
}
/**
 * �ַ����������ַ����ָ�ŵ�vector����
 * @param src �ַ���
 * @param token ��ʲôΪ�ָ�
 * @param vec ����������vector��
 */
void VDbg::str_split(string src, string token, vector<string> & vec)
{
	int nEnd   = 0;
	int nBegin = 0;

	while(nEnd != -1)
	{
		nEnd = src.find_first_of(token, nBegin);
		if(nEnd == -1)
			vec.push_back(src.substr(nBegin, src.length()-nBegin));
		else
			vec.push_back(src.substr(nBegin, nEnd-nBegin));
		nBegin = nEnd + 1;
	}
}
/**
 * �ַ���ȥ�ո�
 */
string VDbg::strTrim(string s)
{
	int i = 0;
	while (s[i]== ' '|| s[i] == ' ')
	{
		i++;
	}
	s = s.substr(i);
	i = s.size()-1;
	while(s[i] == ' '|| s[i] == ' ')
	{
		i--;
	}
	s = s.substr(0,i+1);
	return s;
}
/**
 * �������ascii���������תΪ�ʺ�
 */
char VDbg::ConvertToSafeChar(char ch) {

	if (ch < 0x1E || ch > 0x7F) {
		return '?';
	}

	return ch;
}
/**
 * �������ascii���������תΪ�ʺ�
 */
wchar_t VDbg::ConvertToSafeWChar(wchar_t ch) {

	if (ch < 0x1E) {
		return L'?';
	}

	char buffer[4];

	size_t convertedCount;
	wcstombs_s(&convertedCount, buffer, 4, &ch, 2);

	if (convertedCount == 0) {
		return L'?';
	}
	
	return ch;
}
/**
 * ö�ٿ�ִ��ģ���callback����
 */
BOOL CALLBACK EnumerateLoadedModulesProc64(PCTSTR  ModuleName, DWORD64 ModuleBase, ULONG   ModuleSize,
										   PVOID   UserContext)
{
	MODULE_MAP * pModuleMap = (MODULE_MAP*)UserContext;

	LPCWSTR name = wcsrchr(ModuleName, '\\') + 1;

	(*pModuleMap)[(DWORD)ModuleBase] = name;

	return TRUE;
}
/**
 * ö�پֲ�������callback����
 */
BOOL CALLBACK SymEnumSymbolsLocalProc(PSYMBOL_INFO pSymInfo, ULONG SymbolSize, PVOID UserContext)
{

	if(pSymInfo->Tag == SymTagData)
	{
		map<string, MY_SYMBOL_INFO> * symbol_map = (map<string, MY_SYMBOL_INFO>*)UserContext;
		MY_SYMBOL_INFO * my_sym_info = new MY_SYMBOL_INFO();

		my_sym_info->Address = pSymInfo->Address;
		my_sym_info->nSize = pSymInfo->Size;
		my_sym_info->type  = pSymInfo->TypeIndex;
		my_sym_info->modBase =  pSymInfo->ModBase;
		string strName(pSymInfo->Name);
		my_sym_info->name = strName;

		string temp(pSymInfo->Name);
		(*symbol_map)[temp] = *my_sym_info;
	}
	return true;
}
/**
 * ö��ȫ�ֱ�����callback����
 */
BOOL CALLBACK SymEnumSymbolsProc(PSYMBOL_INFO pSymInfo, ULONG SymbolSize, PVOID UserContext)
{
	if(pSymInfo->Tag == SymTagData)
	{
		map<string, MY_SYMBOL_INFO> * symbol_map = (map<string, MY_SYMBOL_INFO>*)UserContext;
		MY_SYMBOL_INFO * my_sym_info = new MY_SYMBOL_INFO();
		string temp(pSymInfo->Name);
		my_sym_info->Address = pSymInfo->Address;
		my_sym_info->nSize = pSymInfo->Size; // ע�⣡��ֻ����pdb�ļ���ʱ�����SIZE�Żᱻ����������ֵ
		my_sym_info->name = temp;
		my_sym_info->type = pSymInfo->TypeIndex;
		my_sym_info->modBase =  pSymInfo->ModBase;
		(*symbol_map)[temp] = *my_sym_info;
	}
	return true;
}