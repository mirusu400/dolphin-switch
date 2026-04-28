// Newlib symbol gaps on devkitA64 — these symbols are *declared* by
// the toolchain's libc headers but no binary implementation ships in
// libc.a. Each call site that pulled them in is documented at the
// stub site. M5+ revisits once a real frontend exercises the surfaces.
//
// Newlib's <string.h> asm-aliases `basename` to `__gnu_basename` and
// declares it as `char* basename(const char*)`, which conflicts with the
// XPG-style `char* basename(char*)` curl expects. We sidestep the whole
// header by hand-rolling strrchr — this TU does not include any libc
// header that would pull the alias in.

static const char* shim_strrchr(const char* s, int c)
{
    const char* last = (const char*)0;
    while (*s)
    {
        if ((unsigned char)*s == (unsigned char)c)
            last = s;
        ++s;
    }
    if ((unsigned char)c == 0)
        return s;
    return last;
}

// curl's mime.c uses XPG-style basename(char*); newlib's string.h
// aliases the same function to __gnu_basename when GNU_VISIBLE.
// Provide both names so any caller resolves.
char* __gnu_basename(const char* path)
{
    if (!path || !*path)
        return (char*)".";
    const char* slash = shim_strrchr(path, '/');
    return (char*)(slash ? slash + 1 : path);
}

char* basename(char* path)
{
    return __gnu_basename(path);
}

// FileUtil::RenameSync uses dirname (XPG, mutating).
char* dirname(char* path)
{
    if (!path || !*path)
        return (char*)".";
    const char* slash = shim_strrchr(path, '/');
    if (!slash)
        return (char*)".";
    if (slash == path)
        return (char*)"/";
    *((char*)slash) = '\0';
    return path;
}

// ImGui's Platform_OpenInShellFn_DefaultImpl forks/execs to open URLs;
// Switch homebrew never does that.
int execvp(const char* file, char* const argv[])
{
    (void)file;
    (void)argv;
    return -1;
}

int waitpid(int pid, int* wstatus, int options)
{
    (void)pid;
    (void)wstatus;
    (void)options;
    return -1;
}
