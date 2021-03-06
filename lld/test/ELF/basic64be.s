# RUN: llvm-mc -filetype=obj -triple=powerpc64-unknown-linux %s -o %t
# RUN: ld.lld -discard-all %t -o %t2
# RUN: llvm-readobj -file-headers -sections -section-data -program-headers %t2 | FileCheck %s
# REQUIRES: ppc

# exits with return code 42 on linux
.text
	li      0,1
	li      3,42
	sc
# CHECK: ElfHeader {
# CHECK-NEXT:   Ident {
# CHECK-NEXT:     Magic: (7F 45 4C 46)
# CHECK-NEXT:     Class: 64-bit (0x2)
# CHECK-NEXT:     DataEncoding: BigEndian (0x2)
# CHECK-NEXT:     FileVersion: 1
# CHECK-NEXT:     OS/ABI: SystemV (0x0)
# CHECK-NEXT:     ABIVersion: 0
# CHECK-NEXT:     Unused: (00 00 00 00 00 00 00)
# CHECK-NEXT:   }
# CHECK-NEXT:   Type: Executable (0x2)
# CHECK-NEXT:   Machine: EM_PPC64 (0x15)
# CHECK-NEXT:   Version: 1
# CHECK-NEXT:   Entry: 0x10010000
# CHECK-NEXT:   ProgramHeaderOffset: 0x40
# CHECK-NEXT:   SectionHeaderOffset: 0x11050
# CHECK-NEXT:   Flags [ (0x2)
# CHECK-NEXT:     0x2
# CHECK-NEXT:   ]
# CHECK-NEXT:   HeaderSize: 64
# CHECK-NEXT:   ProgramHeaderEntrySize: 56
# CHECK-NEXT:   ProgramHeaderCount: 4
# CHECK-NEXT:   SectionHeaderEntrySize: 64
# CHECK-NEXT:   SectionHeaderCount: 6
# CHECK-NEXT:   StringTableSectionIndex: 4
# CHECK-NEXT: }
# CHECK-NEXT: Sections [
# CHECK-NEXT:   Section {
# CHECK-NEXT:     Index: 0
# CHECK-NEXT:     Name:  (0)
# CHECK-NEXT:     Type: SHT_NULL (0x0)
# CHECK-NEXT:     Flags [ (0x0)
# CHECK-NEXT:     ]
# CHECK-NEXT:     Address: 0x0
# CHECK-NEXT:     Offset: 0x0
# CHECK-NEXT:     Size: 0
# CHECK-NEXT:     Link: 0
# CHECK-NEXT:     Info: 0
# CHECK-NEXT:     AddressAlignment: 0
# CHECK-NEXT:     EntrySize: 0
# CHECK-NEXT:     SectionData (
# CHECK-NEXT:     )
# CHECK-NEXT:   }
# CHECK-NEXT:   Section {
# CHECK-NEXT:     Index: 1
# CHECK-NEXT:     Name: .text (1)
# CHECK-NEXT:     Type: SHT_PROGBITS (0x1)
# CHECK-NEXT:     Flags [ (0x6)
# CHECK-NEXT:       SHF_ALLOC (0x2)
# CHECK-NEXT:       SHF_EXECINSTR (0x4)
# CHECK-NEXT:     ]
# CHECK-NEXT:     Address: 0x10010000
# CHECK-NEXT:     Offset: 0x10000
# CHECK-NEXT:     Size: 12
# CHECK-NEXT:     Link: 0
# CHECK-NEXT:     Info: 0
# CHECK-NEXT:     AddressAlignment: 4
# CHECK-NEXT:     EntrySize: 0
# CHECK-NEXT:     SectionData (
# CHECK-NEXT:       0000: 38000001 3860002A 44000002           |8...8`.*D...|
# CHECK-NEXT:     )
# CHECK-NEXT:   }
# CHECK-NEXT:   Section {
# CHECK-NEXT:     Index: 2
# CHECK-NEXT:     Name: .comment (7)
# CHECK-NEXT:     Type: SHT_PROGBITS (0x1)
# CHECK-NEXT:     Flags [ (0x30)
# CHECK-NEXT:       SHF_MERGE (0x10)
# CHECK-NEXT:       SHF_STRINGS (0x20)
# CHECK-NEXT:     ]
# CHECK-NEXT:     Address: 0x0
# CHECK-NEXT:     Offset: 0x11000
# CHECK-NEXT:     Size: 8
# CHECK-NEXT:     Link: 0
# CHECK-NEXT:     Info: 0
# CHECK-NEXT:     AddressAlignment: 1
# CHECK-NEXT:     EntrySize: 1
# CHECK-NEXT:     SectionData (
# CHECK-NEXT:       0000: 4C4C4420 312E3000                    |LLD 1.0.|
# CHECK-NEXT:     )
# CHECK-NEXT:   }
# CHECK-NEXT:   Section {
# CHECK-NEXT:     Index: 3
# CHECK-NEXT:     Name: .symtab (16)
# CHECK-NEXT:     Type: SHT_SYMTAB (0x2)
# CHECK-NEXT:     Flags [ (0x0)
# CHECK-NEXT:     ]
# CHECK-NEXT:     Address: 0x0
# CHECK-NEXT:     Offset: 0x11008
# CHECK-NEXT:     Size: 24
# CHECK-NEXT:     Link: 5
# CHECK-NEXT:     Info: 1
# CHECK-NEXT:     AddressAlignment: 8
# CHECK-NEXT:     EntrySize: 24
# CHECK-NEXT:     SectionData (
# CHECK-NEXT:       0000: 00000000 00000000 00000000 00000000  |................|
# CHECK-NEXT:       0010: 00000000 00000000                    |........|
# CHECK-NEXT:     )
# CHECK-NEXT:   }
# CHECK-NEXT:   Section {
# CHECK-NEXT:     Index: 4
# CHECK-NEXT:     Name: .shstrtab (24)
# CHECK-NEXT:     Type: SHT_STRTAB (0x3)
# CHECK-NEXT:     Flags [ (0x0)
# CHECK-NEXT:     ]
# CHECK-NEXT:     Address: 0x0
# CHECK-NEXT:     Offset: 0x11020
# CHECK-NEXT:     Size: 42
# CHECK-NEXT:     Link: 0
# CHECK-NEXT:     Info: 0
# CHECK-NEXT:     AddressAlignment: 1
# CHECK-NEXT:     EntrySize: 0
# CHECK-NEXT:     SectionData (
# CHECK-NEXT:       0000: 002E7465 7874002E 636F6D6D 656E7400  |..text..comment.|
# CHECK-NEXT:       0010: 2E73796D 74616200 2E736873 74727461  |.symtab..shstrta|
# CHECK-NEXT:       0020: 62002E73 74727461 6200               |b..strtab.|
# CHECK-NEXT:     )
# CHECK-NEXT:   }
# CHECK-NEXT:   Section {
# CHECK-NEXT:     Index: 5
# CHECK-NEXT:     Name: .strtab (34)
# CHECK-NEXT:     Type: SHT_STRTAB (0x3)
# CHECK-NEXT:     Flags [ (0x0)
# CHECK-NEXT:     ]
# CHECK-NEXT:     Address: 0x0
# CHECK-NEXT:     Offset: 0x1104A
# CHECK-NEXT:     Size: 1
# CHECK-NEXT:     Link: 0
# CHECK-NEXT:     Info: 0
# CHECK-NEXT:     AddressAlignment: 1
# CHECK-NEXT:     EntrySize: 0
# CHECK-NEXT:     SectionData (
# CHECK-NEXT:       0000: 00                                   |.|
# CHECK-NEXT:     )
# CHECK-NEXT:   }
# CHECK-NEXT: ]
# CHECK-NEXT: ProgramHeaders [
# CHECK-NEXT:   ProgramHeader {
# CHECK-NEXT:     Type: PT_PHDR (0x6)
# CHECK-NEXT:     Offset: 0x40
# CHECK-NEXT:     VirtualAddress: 0x10000040
# CHECK-NEXT:     PhysicalAddress: 0x10000040
# CHECK-NEXT:     FileSize: 224
# CHECK-NEXT:     MemSize: 224
# CHECK-NEXT:     Flags [ (0x4)
# CHECK-NEXT:       PF_R (0x4)
# CHECK-NEXT:     ]
# CHECK-NEXT:     Alignment: 8
# CHECK-NEXT:   }
# CHECK-NEXT:   ProgramHeader {
# CHECK-NEXT:     Type: PT_LOAD (0x1)
# CHECK-NEXT:     Offset: 0x0
# CHECK-NEXT:     VirtualAddress: 0x10000000
# CHECK-NEXT:     PhysicalAddress: 0x10000000
# CHECK-NEXT:     FileSize: 288
# CHECK-NEXT:     MemSize: 288
# CHECK-NEXT:     Flags [ (0x4)
# CHECK-NEXT:       PF_R (0x4)
# CHECK-NEXT:     ]
# CHECK-NEXT:     Alignment: 65536
# CHECK-NEXT:   }
# CHECK-NEXT:   ProgramHeader {
# CHECK-NEXT:     Type: PT_LOAD (0x1)
# CHECK-NEXT:     Offset: 0x10000
# CHECK-NEXT:     VirtualAddress: 0x10010000
# CHECK-NEXT:     PhysicalAddress: 0x10010000
# CHECK-NEXT:     FileSize: 4096
# CHECK-NEXT:     MemSize: 4096
# CHECK-NEXT:     Flags [ (0x5)
# CHECK-NEXT:       PF_R (0x4)
# CHECK-NEXT:       PF_X (0x1)
# CHECK-NEXT:     ]
# CHECK-NEXT:     Alignment: 65536
# CHECK-NEXT:   }
# CHECK-NEXT:   ProgramHeader {
# CHECK-NEXT:     Type: PT_GNU_STACK (0x6474E551)
# CHECK-NEXT:     Offset: 0x0
# CHECK-NEXT:     VirtualAddress: 0x0
# CHECK-NEXT:     PhysicalAddress: 0x0
# CHECK-NEXT:     FileSize: 0
# CHECK-NEXT:     MemSize: 0
# CHECK-NEXT:     Flags [ (0x6)
# CHECK-NEXT:       PF_R (0x4)
# CHECK-NEXT:       PF_W (0x2)
# CHECK-NEXT:     ]
# CHECK-NEXT:     Alignment: 0
# CHECK-NEXT:   }
# CHECK-NEXT: ]
