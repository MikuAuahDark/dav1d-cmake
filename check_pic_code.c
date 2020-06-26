#if defined(PIC)
#error "PIC already defined"
#elif !(defined(__PIC__) || defined(__pic__))
#error "no pic"
#endif
