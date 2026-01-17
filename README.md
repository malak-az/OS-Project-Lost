# OS Project (FOS)

This repository contains an educational operating system project developed using the **FOS framework**.  
The project focuses on understanding how an operating system manages memory, processes, and system protection.

The implementation is divided into **two main stages**, where each stage adds new core OS functionality.

---

## Stage 1

The first stage focuses on basic kernel memory management.

### Implemented features:
- **Dynamic Memory Allocator**  
  Handles dynamic allocation and deallocation inside the kernel.
  
- **Kernel Heap**  
  Manages dynamic memory used by the kernel.
  
- **Page Fault Handler (Replacement)**  
  Handles page faults caused by page replacement.


---

## Stage 2

The second stage extends the system with more advanced OS modules.

### Implemented modules:
- **Page Fault Handler (Placement)**  
  Handles page faults caused by missing pages.

- **User Heap**  
  Provides dynamic memory allocation for user programs.

- **Shared Memory**  
  Allows processes to share memory for communication.

- **CPU Scheduling**  
  Manages how processes are scheduled on the CPU.

- **Kernel Protection**  
  Protects kernel memory from illegal user access.

---

## Tools & Environment

- Language: C  
- Emulator: Bochs / QEMU  
- Build system: Makefile  
- Framework: FOS

---
