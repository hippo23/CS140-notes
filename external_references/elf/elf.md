# Executable-Linkable Format (ELF) in UNIX Systems

- It was original published as part of the Application Binary Interface (ABI), which programs use to interact with the kernel and hence all the hardware (it's like a standard, with conventions and rules for how exceptions are handled).
  - Address width, passing system calls, run-time stack, format of system libraries, etc.

## Understanding Linkable vs Relocatable ELF files

- If you compile C code into a .o file, you will see the following:

```c
// methods.c
int add(int a, int b) {
  return a + b;
}

int subtract(int a, int b) {
  return a - b;
}
```

```c
// main file
#include "methods.h"
#include <stdio.h>

int main() {
  int a, b;
  a = 5;
  b= 7;
  int c = add(a, b);

  printf("%d\n", c);
}
```

- If we try and compile `main` directly into an executable without including the other `c` file, we get the following error:

```txt
Undefined symbols for architecture arm64:
  "_add", referenced from:
      _main in main-5cbbf4.o
ld: symbol(s) not found for architecture arm64
clang: error: linker command failed with exit code 1 (use -v to see invocation
```

- The `ld` or linker failed to find a symbol (meaning some function, if you think of it in assembly terms) and where it was defined. Why is that?
- It's because

## Object File Format of ELF

- There are different views depending on which part of the compilation process we are in. When we are trying to link object files (which is often the assembly code that the compiler generated), we use the linking view, when executing, use the execution view.
- They have some parts in common, namely:
  - **ELF header** - Think of this as a table of contents for the rest of the file, dictating its format (the symbol table is also here, remember how segment linking works?)
  - **Program header table** - Tells the system how to create a process image. Files that are used to build a process image (execute a program) need this, otherwise, they do not need one.
  - **Section header table** - Information describing the various sections of the file that can be linked. Each section has an entry in the table, and has its name, size, etc.

### Data Representation

- The object file format supports processors with 8-bit bytes and 32-bit architecture, but the behaviour can be modified. Hence, we have some ways to change how the file is interpreted. In some cases, the data-types are platform independent. See the following:

```c
  typedef  Elf32_Addr uint32_t; // unsigned program address;
  typedef  Elf32_Half uin16_t; // unsigned medium integer;
  typedef  Elf32_Off  uint32_t; // unsigned file offset;
  typedef  Elf32_Sword uint32_t; // signed large integer;
  typedef  Elf32_Word uint32_t; // unsigned large integer;
  typedef  unsigned_char uint8_t; // unsigned small integer;
```

### ELF Header

```c
#define EI_NIDENT16
typedef struct {
unsigned chare_ident[EI_NIDENT]; // used to identify the file as an object file and provide machine independent data;
Elf32_Half e_type; // ET_NONE, ET_REL, ET_EXEC, ET_DYN, ET_CORE, ET_LOPROC, ET_HIPROC
Elf32_Half e_machine; // specifices required architecture for an individual file
Elf32_Word e_version ;
Elf32_Add re_entry;
Elf32_Off e_phoff;
Elf32_Off e_shoff;
Elf32_Word e_flags;
Elf32_Half e_ehsize;
Elf32_Half e_phentsize;
Elf32_Half e_phnum;
Elf32_Half e_shentsize ;
Elf32_Half e_shnum;
Elf32_Half e_shstrndx ;
} Elf32_Ehdr
```

#### e_machine

| Name    | Value | Meaning             |
| ------- | ----- | ------------------- |
| EM_NONE | 0     | No machine required |
| EM_M32  | 1     | At&T                |
| ...     | ...   | ...                 |

- The value dictates the specific architecture needed for an individual file

#### e_version

| Name       | Value | Meaning         |
| ---------- | ----- | --------------- |
| EV_NONE    | 0     | Invalid Version |
| EV_CURRENT | 1     | Current Version |

- This determines the object file version. 1 is the original file format, and `EV_CURRENT`, though given as 1, will change as necessary to reflect the current version number.

#### e_entry

- Gives the virtual address to which the system first transfers control, thus starting the process. If there is no entry-point, this holds zero.

#### e_shoff

- Holds the section header table's file offset in bytes (how far from the start of the file the section header table begins).

#### e_flags

- Holds processor specific flags associated with the file. They take the form `EF_<machine>_<flag>`

#### e_ehsize

- Holds the ELF header's size in bytes.

#### e_phentsize

- Holds the size in bytes of entry entry in the file's program header table; all entries are the same size.

#### e_phnum

- Holds the number of entries in the program header's table.

#### e_shentsize

- Holds a section header table entry size in bytes.

#### e_shnum

- Holds the size of a section header table.

#### e_shstrndx

- Holds the section header table index of the entry associated with the section name string table.

### ELF Identification

- To support the framework that allows ELF to work with multiple kinds of processors, data encodings, and classes of machines, we have the following in the ELF header. The initial bytes of the ELF header correspond to the [e_ident](#elf-header) attribute.

| Name       | Index | Purpose                |
| ---------- | ----- | ---------------------- |
| EI_MAG0    | 0     | File Identification    |
| ...        | ...   | ...                    |
| EI_CLASS   | 4     | File class             |
| EI_DATA    | 5     | Data encoding          |
| EI_VERSION | 6     | File version           |
| EI_PAD     | 7     | Start of padding bytes |
| EI_NINDENT | 16    | Aize of e_ident[]      |

- Each index within the array hold the following data:

#### EI_MAG0 to EI_MAG3

| Name    | Value | Position           |
| ------- | ----- | ------------------ |
| ELFMAG0 | 0x7f  | `e_ident[EI_MAG0]` |
| ...     | 'E'   | ---                |
| ...     | 'L'   | ---                |
| ...     | 'F'   | ---                |

- This is what identifies the file as an object file.

#### EI_CLASS

- This identifies the file's class or capacity.

| Name         | Value | Meaning                                                                           |
| ------------ | ----- | --------------------------------------------------------------------------------- |
| ELFCLASSNONE | 0     | Invalid class                                                                     |
| ELFCLASS32   | 1     | 32-bit objects; Supports machines with files and virtual address spaces up to 4GB |
| ELFCLASS64   | 2     | 64-bit objects                                                                    |

#### EI_DATA

- The byte at `e_ident[EI_DATA]` specifies data encoding of processor-specific data in the object file.

| Name        | Value | Meaning                                        |
| ----------- | ----- | ---------------------------------------------- |
| ELFDATANONE | 0     | Invalid data encoding                          |
| ELFDATA2LSB | 1     | Least significant byte occupies lowest address |
| ELFDATA2MSB | 2     | Most significant byte occupies highest address |

#### EI_VERSION

- Byte `e_ident[]` specifies the ELF header version number, this must be equal to `EV_CURRENT`.

#### EI_PAD

- Beginning of the unused bytes in `e_ident`. Set to zero and should be ignored by those reading object files.

### Machine Information

- Again, we just use the [EI_DATA](#eidata) and [EI_CLASS](#eiclass) to figure out how to interpret values.

### Sections

#### Section Header Table

- Using the file section header, we are given the ability to locate all of the file's sections.
- The section header is an array of `Elf32_Shdr` structures. `e_shoff` of the [elf header](#elf-header) is used to find the offset from the beginning of the file to the section header table.
- `e_shnum` says how many entries; `e_shentsize` gives the size per entry.
- Some of the indexes are reserved, the object file will not have entries for these indexes, rather, they have some information about or for the sections.

| Name          | Value  | Meaning                                             |
| ------------- | ------ | --------------------------------------------------- |
| SHF_UNDEF     | 0      | Marks an undefined or irrelevant section reference  |
| SHN_LORESERVE | 0xff00 | Lower bound of the range of reserved indexes        |
| SHN_LOPROC    | 0xff00 | Values in this range - `SHN_HIPROC` for the process |
| SHN_HIPROC    | 0xff1f | Look above                                          |
| SHN_ABS       | 0xfff1 | Specifies absolute values for the references        |
| SHN_COMMON    | 0xfff2 | common symbols i.e. unallocated C variables         |
| SHN_HIRESERVE | 0xffff | Upper bound of the range of reserved indexes        |

- **QUESTION: What is meant by common symbols? Why are only unallocated C variables counted as common symbols?**
- Note that `SHIN_ABS`, that what we mean is that symbols defined relative to `SHN_ABS` have absolute values and are not affected by relocation.
- An object file's sections must satisfy these conditions:
  - Every section in an object file has exactly one section header describing it.
  - Each section is contiguous within a file
  - Sections in a file may not overlap
  - An object file may have inactive space, content is not defined.

#### Section Header

```c
Elf32_Word sh_name; // unsigned large integer, but as a name
Elf32_Word sh_type;
Elf32_Word sh_flags;
Elf32_Addr sh_addr;
Elf32_Off sh_offset;
Elf32_Word sh_size;
Elf32_Word sh_link;
Elf32_Word sh_info;
Elf32_Word sh_addralign;
Elf32_Word sh_entsize;
```

- `sh_name` - Specifies the name of the section, and its value is an index into the section header string table.
- `sh_type` - Categorizes the section's contents and semantics
- `sh_flags` - Sections support 1-bit flags that describe some attributes.
- `sh_addr` - If the section will appear in memory (does this mean some sections won't be loaded, if we have an executable?) this gives the address at which the section's first byte should reside (is that a virtual or physical address? Should it matter?)
- `sh_offset` - Byte offset from beginning of file to first byte of the section.
- `sh_size` - The section's size in bytes.
- `sh_link` - Section header table index link, interpretation depends on the type.
- `sh_info` - Extra information that depends on the section type.
- `sh_addralign` - Some sections have alignment constraints wherein, if for example, a section holds a `doubleword`, all parts must be `doubleword`-aligned.
- `sh_entsize` - A table of fixed-size entries such as a symbol table. This gives the size in bytes of each entry.

- An `sh_type` can hold the corresponding values:

| Name         | Value      | Meaning                                               |
| ------------ | ---------- | ----------------------------------------------------- |
| SHT_NULL     | 0          | section header is inactive                            |
| SHT_PROGBITS | 1          | section holds info defined by the program             |
| SHT_SYMTAB   | 2          | Holds a symbol table                                  |
| SHT_STRTAB   | 3          | String table                                          |
| SHT_RELA     | 4          | Relocation entries with explict addends (what?)       |
| SHT_HASH     | 5          | symbol hash table                                     |
| SHT_DYNAMIC  | 6          | info for dynamic linking                              |
| SHT_NOTE     | 7          | marks the file in some way                            |
| SHT_NOBITS   | 8          | no space in the file but otherwise resembles progbits |
| SHT_REL      | 9          | Relocation entries without explicit addends           |
| SHT_SHLIB    | 10         | Reserved but has unspecified semantics                |
| SHT_DYNSM    | 11         | Holds symbol table                                    |
| SHT_LOPROC   | 0x70000000 | Reserved for processor-specific semantics             |
| SHT_HIPROC   | 0x7fffffff | Reserved for processor-specific semantics             |
| SHT_LOUSER   | 0x80000000 | Lower bound of range of indexes reserved for apps     |
| SHT_HIUSER   | 0xfffffff  | Upper bound of indexes reserved for apps              |

- `sh_flag` can have the following values:

| Name          | Value      | Meaning                                            |
| ------------- | ---------- | -------------------------------------------------- |
| SHF_WRITE     | 0x1        | section has data that is writable                  |
| SHF_ALLOC     | 0x2        | section occupies memory during exection of process |
| SHF_EXECINSTR | 0x4        | has executable machine instructions                |
| SHF_MASKPROC  | 0xf0000000 | all bits in mask are for processor semantics       |

- `sh_link` and `sh_info` have special information depending on `sh_type`

| `sh_type`               | `sh_link`                                               | `sh_info`                         |
| ----------------------- | ------------------------------------------------------- | --------------------------------- |
| SHT_DYNAMIC             | section header index of the string table                | 0                                 |
| SHT_HASH                | section header index of the symbol table for hash table | 0                                 |
| SHT_REL / SHT_RELA      | sh.index of associated symbol table                     | sh.index where relocation applies |
| SHT_SYMTAB / SHT_DYNSYM | Info is operating system specific                       | OS specific                       |
| other                   | SHF_UNDEF                                               | 0                                 |

### Special Sections

- Some sections hold program and control information. In the list below, we list some of these special-case systems:

| Name        | Type         | Attributes                |
| ----------- | ------------ | ------------------------- |
| .bss        | SHT_NOBITS   | SHF_ALLOC + SHF_WRITE     |
| .comment    | SHT_PROGBITS | none                      |
| .data       | SHT_PROGBITS | SHF_ALLOC + SHF_WRITE     |
| .data1      | SHT_PROGBITS | SHF_ALLOC + SHF_WRITE     |
| .debug      | SHT_PROGBITS | none                      |
| .dynamic    | SHT_DYNAMIC  | see below                 |
| .dynstr     | SHT_STRTAB   | SHF_ALLOC                 |
| .dynsym     | SHT_DYNSYM   | SHF_ALLOC                 |
| .fini       | SHT_PROGBITS | SHF_ALLOC + SHF_EXECINSTR |
| .got        | SHT_PROGBITS | see below                 |
| .hash       | SHT_HASH     | SHF_ALLOC                 |
| .init       | SHT_PROGBITS | SHF_ALLOC + SHF_EXECINSTR |
| .interp     | SHT_PROGBITS | see below                 |
| .line       | SHT_PROGBITS | none                      |
| .note       | SHT_NOTE     | none                      |
| .plt        | SHT_PROGBITS | see below                 |
| .rel*name*  | SHT_REL      | see below                 |
| .rela*name* | SHT_RELA     | see below                 |
| .rodata     | SHT_PROGBITS | SHF_ALLOC                 |
| .rodata1    | SHT_PROGBITS | SHF_ALLOC                 |
| .shstrtab   | SHT_STRTAB   | none                      |
| .strtab     | SHT_STRTAB   | see below                 |
| .symtab     | SHT_SYMTAB   | see below                 |
| .text       | SHT_PROGBITS | SHF_ALLOC + SHF_EXECINSTR |

## String Table
