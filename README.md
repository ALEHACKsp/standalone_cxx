<img src="https://i.postimg.cc/qRM8QQqp/Standalonec-Reduced.png" alt="logo" height="300" />

## Description

Standalone C++ is an open source, freestanding implementation of C++ led by Assured Information Security, Inc. (AIS),
that leverages Libc++ from the LLVM project. This project provides the ability to run C++ from anywhere, even in environments that do not support C++ including UEFI, IoT, Automotive, Embedded Systems, Hypervisor, Unikernels, Windows/Linux kernel modules or any other freestanding environment (where -ffreestanding is enabled). The goal of this project is to enable the use of C++ from any environment. 

## Dependencies:

To compile Standalone C++, you must first install the following on your system (depending on your OS)

### Arch Linux

```
sudo pacman -S git base-devel clang cmake
```

### Ubuntu 19.04 (or Higher):

```
sudo apt-get install git build-essential clang cmake
```

### Windows 10

```
TBD
```

## Compilation and Testing

The following provides instructions for how to compile and test the Standalone C++ SDK. Once the SDK is compiled, you can test to make sure it is working by running `make quick`. The Standalone C++ SDK provides a set of examples on how to use the SDK with your own projects. The main example is a [test](https://github.com/Bareflank/standalone_cxx/tree/master/examples/tests) application 
and a [loader](https://github.com/Bareflank/standalone_cxx/tree/master/examples/loader) which loads the test C++ application into another application (i.e. running a C++ application from another C++ application). The `make quick` make target uses the loader to execute the test application. The examples also include a UEFI application that runs C++ from a UEFI application. 

### Linux

```
cd $HOME

mkdir working
mkdir working/build
mkdir working/prefix
mkdir working/cache

git clone https://github.com/Bareflank/standalone_cxx.git working/standalone_cxx
cd working/build

cmake ../standalone_cxx
make -j<# cores>

make quick
# Hello CppCon!!!
```

### Windows 10

TBD

## Usage

Standalone C++ is broken up into two distinct components: the C++ application you are writing and a loader. 

### The C++ Application

To write your own C++ application, you first start with some C++ as follows:

#### main.cpp
```cpp
#include <iostream>

int main(int argc, char *argv[])
{
    std::cout << "Hello CppCon!!!\n";
    return 0;
}
```

Once you have a C++ application written, you will need a way to build the application. To do this, we will use CMake. The CMake script that you will use is the default CMake script that you might already be used to, with the addition of the `standalone_cxx` interface library. 

#### CMakeLists.txt
```cmake
cmake_minimum_required(VERSION 3.13)
project(test CXX)

find_package(standalone_cxx)

add_executable(test main.cpp)
target_link_libraries(test PRIVATE standalone_cxx)
```

As shown above, you start with defining your CMake minimum version, as well as the name of your project and its type (which is CXX for C++). From there you locate the standalone_cxx package. Assuming you have compiled the standalone_cxx project, CMake should be able to automatically locate the resulting package for you. Finally, you must create an executable from your C++ code and link it to the standalone_cxx interface library, which contains all of the required includes, libraries and compiler settings for the project. Feel free to add your own as well. Note that some C++ flags like `-mno-red-zone` need to be included when compiling the standalone_cxx project itself which can be done on the command line, and once included, the flags will be included in the resulting interface library so you do not need to include them in your project again. 

Once you have your source and build scripts complete, you can compile your application using the normal `cmake .; make` commands. 

### The Loader

Although the C++ application that you just wrote above is compiled from a freestanding (i.e., compiled with `-ffreestanding`), we still need a way to execute the C++ application from a freestanding environment. To do this, we must load the C++ application into our environment and then execute it. This extra step is needed because unlike C, which can be compiled into a flat binary and executed directly (mostly), C++ relies on several tables of information that are external to the executable code, something C does not rely on. These extra tables include the exception tables, init/fini routines for global construction and thread local storage (TLS). 

To create the loader, we first need to include some headers (in this example we will use UEFI):

```c
#include <efi.h>
#include <efilib.h>

#define BFALERT(...) Print(L"[BAREFLANK ALERT]: " __VA_ARGS__)

#include <bfexec.h>
#include <cxx_uefi.h>
```

The first two headers are the standard UEFI headers. These are only needed to define the standard entry point for our loader as well as gain access to UEFI's memory allocation/free functions. There are the only two dependencies that the loader has (i.e., you must be able to allocate and free memory, something you can always write yourself if such functions do not exist in your environment). The BFALERT macro is needed because the SDK doesn't know how to log errors when they occur. For some environments, this is pre-defined, but on most, you will need to provide your own, or just leave the macro empty if console output is not possible. The `bfexec.h` is the header-only library that we include to load our C++ application. This header-only library has very few include dependencies which are only for defining platform types like `uint64_t` and `size_t`. Finally, we include the cxx_uefi.h header which is our C++ application. To generate this include, we use `xxd` to turn the C++ application into a header (i.e., the executable is converted into a series of byte commands that are loaded into a really large C-style array that gets included in your loader when you include the header itself). 

Next, we need to define some helper functions for the loader. These wrap the functions that bfexec relies on:
```c
void *
platform_alloc(size_t size)
{
    EFI_STATUS status;
    EFI_PHYSICAL_ADDRESS addr = 0;

    status = gBS->AllocatePages(
                 AllocateAnyPages, EfiRuntimeServicesCode, (size / EFI_PAGE_SIZE) + 1, &addr
             );

    if (EFI_ERROR(status)) {
        BFALERT("platform_alloc: AllocatePages failed: %lld\n", size);
    }

    return (void *)addr;
}
```

The first helper function that we need is the `alloc` function, which has the same signature as a `malloc()` function. This function is used to allocate memory for the C++ application. Specifically, bfexec will use this to allocate a stack, a TLS block and memory for the C++ application itself to execute from. If an allocation function is not provided to you, you can always grab a giant chunk of memory and divy it up as this function is called using a simple linear allocator (similar to how sbrk works). 

```c
void
platform_free(void *ptr, size_t size)
{
    gBS->FreePages(
        (EFI_PHYSICAL_ADDRESS) ptr, (size / EFI_PAGE_SIZE) + 1
    );
}
```

The next function is the `free` function. This releases any previously allocated memory when the C++ application is done executing. If you don't care about leaking memory, you can simply leave this function blank. 

```c
status_t
platform_mark_rx(void *addr, size_t size)
{
    bfignored(addr);
    bfignored(size);

    return BFSUCCESS;
}
```

The third helper function is the `mark_rx` function. This function is optional, and is only required if the memory that is returned by the `alloc` function is not marked as read/write/execute. In the case of UEFI, the memory is read/write/execute so this function is not needed, but if you are running on a system that allows you to allocate read/write memory and then mark portions of this memory as read/execute later, we advise implementing the mark_rx function as it will provide better security. For example, to do this on Linux, use the following:
```c
status_t
platform_mark_rx(void *addr, size_t size)
{
    if (mprotect(addr, size, PROT_READ|PROT_EXEC) != 0) {
        return BFFAILURE;
    }

    return BFSUCCESS;
}
```

The `mark_rx` function above uses mprotect to mark memory as read/execute. The bfexec library will use this function to mark the read/execute portions of the C++ application for you. All you need to do is provide the function itself. 

```c
void
platform_syscall_write(struct bfsyscall_write_args *args)
{
    size_t i = 0;

    switch(args->fd) {
        case 1:
        case 2:
            for (i = 0; i < args->nbyte; i++) {
                Print(L"%c", ((char *)args->buf)[i]);
            }
            args->ret = args->nbyte;
            args->error = 0;
            return;

        default:
            return;
    }
}

void
platform_syscall(uint64_t id, void *args)
{
    switch(id) {
        case BFSYSCALL_WRITE:
            return platform_syscall_write(args);

        default:
            return;
    }
}
```

Finally, we can also, optionally, provide a `syscall` function. This function is used to handle syscalls that your C++ application might attempt to make. This includes things like console requests and file operations. If your C++ application doesn't have any of these requirements, feel free to not implement this function. In the case above, since we are adding C++ to UEFI, we have access to a console, so we implement the "write" `syscall` which on a Unix system, writes to a console when the file descriptor is 1 (for stdout) or 2 (for stderr). Also note that we loop through each character being outputted to the console because UEFI takes 16bit characters while the C++ application outputs 8bit characters when std::cout is used. The [bfsyscall.h](https://github.com/Bareflank/standalone_cxx/blob/master/bfsdk/include/bfsyscall.h) header provides a complete list of system calls that are currently supported. 

In order to provide these functions to the bfexec library, we must store pointers to our helper functions in a struct as follows:
```c
struct bfexec_funcs_t funcs = {
    platform_alloc,
    platform_free,
    platform_mark_rx,
    platform_syscall
};
```

If you don't have a mark_rx or syscall function, just use NULL instead. The last step is to execute our C++ application as follows:
```
EFI_STATUS
efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *systab)
{
    InitializeLib(image, systab);
    return bfexec((char *)cxx_uefi, cxx_uefi_len, &funcs);
}
```

The bfexec() function takes a pointer to the C++ application you wish to execute. This could be a buffer you allocate and load with the C++ application from disk, or it could be a pointer to an array that has the C++ application pre-populated as we do using `xxd`. Finally the bfexec takes the size (in bytes) of the C++ application and a pointer to our helper functions, returning the results of the main() function in your C++ application. 

That's it! That is all you need to run C++ from anywhere. 

## License

The Bareflank Hypervisor is licensed under the MIT License.

