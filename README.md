# VDebuggerDos

A GDB-Like C/C++ Debugger that implements most debugger functionality like stepping, breakpoints, tracking variables, printing stack trace, and etc.

# Usage
  cd VDebuggerDos/VDbg/Debug/
  VDbg.exe yourprogram.exe
  Followings are all the availiable commands:
  1.  run (run the program, will stop when a break point is hit)
  2.  cont (continue the program, will stop when a break point is hit)
  3.  step (step over)
  4.  stepi (step into)
  5.  stepo (step out)
  6.  break [file]:[line number] (set break point)
  7.  rm [file]:[line number] (remove break point)
  8.  bpinfo (show current break point information)
  9   stack (show stack information)
  10  memory [memory address]:[how many bytes, in hex]
  11. print [variable]"
  12. locals (show all local variables)
  13. globals (show all global variables)
  14. quit
