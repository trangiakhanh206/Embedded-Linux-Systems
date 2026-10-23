# Embedded Linux Systems

Repository containing Embedded Linux laboratory exercises
and programming assignments.

## Repository Structure

| Folder | Description |
|---|---|
| LAB01 | Linux Kernel, U-Boot, BusyBox and QEMU setup |
| LAB02 | Linux Kernel Module, procfs and sysfs |
| LAB03 | Debugging, memory analysis and performance profiling |
| ASSIGNMENT_01 | Kernel drivers, IOCTL and assignment reports |
| ASSIGNMENT_02 | Multithreaded Sensor Monitoring System |

## LAB01 - Embedded Linux Environment

- Linux Kernel 5.15
- U-Boot 2022.04
- BusyBox 1.35.0
- ARM Cortex-A9 / QEMU Versatile Express
- Kernel, DTB, U-Boot and initramfs build outputs

## LAB02 - Linux Device Driver

- Custom Linux Kernel Module
- Character device interface
- procfs and sysfs integration
- Driver verification script

## LAB03 - Debugging and Profiling

- GDB debugging
- Valgrind memory analysis
- strace system call tracing
- perf profiling
- Latency histogram

## ASSIGNMENT 01

- Task 1: Character device driver and IOCTL
- Task 2: Counter driver
- Task 3: Technical report

## ASSIGNMENT 02 - Sensor Monitoring System

The system includes:

- Sensor Thread
- Processor Thread
- Alert Thread
- Logger Thread
- Shared buffer and synchronization
- POSIX message queues
- Linux device driver

### Task 4 - Real-Time Scheduling Results

Environment: Ubuntu Host simulation

Sensor interval: 500 ms

Samples per policy: 1,021

| Metric | SCHED_OTHER | SCHED_FIFO |
|---|---:|---:|
| Min jitter (us) | 46.220 | 6.777 |
| Average jitter (us) | 228.032 | 17.005 |
| Max jitter (us) | 953.648 | 330.581 |
| P99 jitter (us) | 845.043 | 59.992 |

**Observed P99 jitter reduction: 92.90%**

Both scheduling policies were verified using
pthread_getschedparam().

Full results:
[Task 4 Summary](ASSIGNMENT_02/logs/task4/task4_summary.txt)

## Tools and Technologies

- Ubuntu Linux
- QEMU ARM
- GCC and ARM cross-compiler
- Linux Kernel Modules
- POSIX Threads
- GDB
- Valgrind
- strace
- perf
- Python

## Notes

Large upstream Linux Kernel, U-Boot and BusyBox
source trees are not included.

Selected configuration files, application source
code, build outputs and experiment evidence
are provided.

Real-time jitter results were measured on the
Ubuntu Host and should not be interpreted as
guaranteed worst-case latency on ARM hardware.
