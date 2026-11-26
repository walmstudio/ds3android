/* ARM64 ELF fixtures for metadata/staging tests only; never dlopened or executed. */
#ifdef HAL_ONLY
int HMI;
#else
#ifdef PRIVATE_DEPENDENCY
extern int fixture_dep(void);
#endif
#ifndef NO_NEGOTIATE
int vk_icdNegotiateLoaderICDInterfaceVersion(void) {
#ifdef PRIVATE_DEPENDENCY
    return fixture_dep();
#else
    return 0;
#endif
}
#endif
void *vk_icdGetInstanceProcAddr(void) { return 0; }
#endif
