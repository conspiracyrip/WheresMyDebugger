# Where's my debugger man!?!?
## took my debugger too? cant have shit in detroit..
<img width="1023" height="1000" alt="image" src="https://github.com/user-attachments/assets/a577af15-a686-4f6a-9fe6-c0be2e7643d3" />

# ok hows it work?
### works by injecting known debuggers windows to processes then checking if the window magically disappears.

# important notes:
- requires /GS- or the compiler will destroy the injected function. (not like it does anything anyways)
- does not free memory.
- x64 only (easily portable to x86 though i dont like x86)
- crashes processes/race condition/false positives a lot, use like 5 different and check on the local process before accurately determining
- i thought this was funny as a concept, works on Hyperhide and almost every antidebug tool..
- current in PoC state not stable at all. left some unused wrappers in case you want to expand on this
- please fork with fixes
