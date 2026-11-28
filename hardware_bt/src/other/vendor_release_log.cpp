// Only the release Paradise archive redirects its logging imports here.
// Authentication prompts and ordinary application stdio still use libc.
extern "C" int tb_vendor_printf(const char*, ...) { return 0; }
extern "C" int tb_vendor_puts(const char*) { return 0; }
