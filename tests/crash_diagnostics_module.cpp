// A deliberate instruction trap provides a PC inside a library loaded after
// diagnostics installation, without dereferencing invalid memory.
extern "C" void DiagnosticTestTrap() { __builtin_trap(); }
