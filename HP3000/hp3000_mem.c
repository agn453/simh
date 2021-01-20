/* hp3000_mem.c: HP 3000 main memory simulator

   Copyright (c) 2016-2020, J. David Bryan

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
   AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
   ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
   WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of the author shall not be used
   in advertising or otherwise to promote the sale, use or other dealings in
   this Software without prior written authorization from the author.

   MEM          HP 3000 Series III Main Memory

   25-Sep-20    JDB     Added "mem_reset_byte" routine
   23-Sep-20    JDB     Changed uint8 uses to HP_BYTE
   09-Dec-19    JDB     Replaced debugging macros with tracing macros
   27-Dec-18    JDB     Revised fall through comments to comply with gcc 7
   21-May-18    JDB     Changed "access" to "mem_access" to avoid clashing
   10-Oct-16    JDB     Created

   References:
     - HP 3000 Series II/III System Reference Manual
         (30000-90020, July 1978)
     - HP 3000 Series III Engineering Diagrams Set
         (30000-90141, April 1980)


   The HP 3000 Memory Subsystem is an integral part of the 3000 computer.
   Replacing the core memory used in the earlier 3000 CX machines, the Series II
   introduced an all-semiconductor memory using 4K NMOS RAMs that provided error
   detection and correction.  Single-bit errors are corrected automatically, and
   double-bit errors are detected.  All errors are logged in hardware, and the
   logs are downloaded periodically by MPE to allow preventative maintenance and
   replacement of failing parts.

   The Series II supports a main memory size of 64K to 256K words in 32K
   increments.  It uses four types of memory PCAs:

     - 30007-60002 MCL (Memory Control and Logging, up to 128K words)
     - 30008-60002 SMA (Semiconductor Memory Array, 32K words, 17 bits)
     - 30009-60001 FCA (Fault Correction Array, up to 128K words, 4 bits)
     - 30009-60002 FLI (Fault Logging Interface, up to 256K words)

   A 64K system uses one of each PCA.  A 256K system uses 2 MCLs, 8 SMAs, 2 FCAs
   and 1 FLI.  Five check bits (one on the SMA, four on the FCA) are used.

   The Series III supports a main memory size of 128K to 1024K words in 128K
   increments using 16K RAMs.  It uses three types of memory PCAs:

     - 30007-60005 MCL (Memory Control and Logging, up to 512K words)
     - 30008-60003 SMA (Semiconductor Memory Array, 128K words, 22 bits)
     - 30009-60002 FLI (Fault Logging Interface, up to 1024K words)

   A 128K system uses one of each PCA.  A 1024K system uses 2 MCLs, 8 SMAs, and
   1 FLI.  Six check bits (all on the SMA) are used.  The standalone FLI PCA may
   be replaced with a 30135-60063 System Clock/Fault Logging Interface that
   combines both devices on a single PCA.

   Main memory consists of from one to eight 128K word memory arrays.  Memory is
   divided into two 512K modules, each with its own Module Control Unit and
   Memory Control and Logging PCA.  The two modules respond to module numbers 0
   and 1 or 2 and 3.

   Error correction is implemented by storing five (Series II) or six (Series
   III) check bits with the sixteen data bits.  The Series III check bits
   reflect the parity of sets of eight data bits, as follows:

     0  1  2  3  4  5  6  7  8  9  10 11 12 13 14 15 C0 C1 C2 C3 C4 C5 Parity
     -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- ------
     X  X  X  X  X  X  X  X                                         X   Even
     X  X  X  X              X  X  X  X                          X      Odd
     X           X  X        X  X        X  X  X              X         Even
        X        X     X     X     X     X  X     X        X            Odd
           X        X     X        X  X  X     X  X     X               Even
              X        X  X     X     X     X  X  X  X                  Even
     -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
     07 13 23 03 15 25 11 21 16 06 32 22 34 14 24 30 00 20 10 04 02 01  Syndrome

   The check bits are generated by setting C0-C5 to zero.  When read, the parity
   computations (syndrome) will result in all zeros if the data and check bits
   are correct and non-zero values if one or more bits are in error.  If a
   single bit (either data or check) is in error, the syndrome itself will have
   odd parity and will indicate the bit in error as indicated above.  If the
   syndrome is non-zero and has even parity, i.e., does not contain either one
   or three 1-bits, then a double-bit error has occurred, and the syndrome value
   is not significant.

   The MCL will correct single-bit data errors (single-bit check errors need not
   be corrected).  Double-bit errors will result in data parity interrupts.

   Each MCL contains one 1024 x 1 static RAM ELA (Error Logging Array).  The
   array stores a 1 in an address corresponding to the 4K or 16K RAM chip
   containing the bit in error.  The address is 10 bits wide, consisting of a
   5-bit chip-row address (2-bit SMA PCA address and 3-bit row address) and a
   5-bit bit-in-error code (the lower five bits of the 6-bit ECC syndrome).  The
   bit-in-error code is decoded as:

     Code  Bit   Code  Bit   Code  Bit   Code  Bit
     ----  ---   ----  ---   ----  ---   ----  ---
      00   C0     10   C2     20   C1     30   D15
      01   C5     11   D6     21   D7     31   --
      02   C4     12   **     22   D11    32   D10
      03   D3     13   D1     23   D2     33   --
      04   C3     14   D13    24   D14    34   D12
      05   *      15   D4     25   D5     35   --
      06   D9     16   D8     26   --     36   --
      07   D0     17   --     27   --     37   --

     *  Forced double-error write
     ** Missing SMA

   If a parity error occurs on the data sent from the MCU to the SMA for a
   write, the MCL asserts a data parity error (CPX1.6) and forces a double-bit
   error into the check bits by complementing the C3 and C5 bits.  This ensures
   that a read of the location will always cause a data parity error interrupt.
   If an addressed SMA is not present, the all-zeros data and check bits result
   in a syndrome of 12, due to the odd parity of the C2 and C4 calculations.


   Main memory is simulated by allocating an array of MEMORY_WORDs large enough
   to accommodate the largest system configuration (1024 KW).  Array access is
   then restricted to the configured size; accesses beyond the end of configured
   memory result in an Illegal Address interrupt.

   All accesses to main memory are through exported functions.  Examine and
   deposit routines provide for SCP interfacing, and general read and write
   routines are used by the other HP 3000 simulator modules.  Each general
   access carries an access classification that determines how memory will be
   addressed.  Program, data, and stack accesses use their respective memory
   bank registers to form the indices into the simulated memory array.  DMA
   accesses on behalf of the multiplexer and selector channels use the memory
   banks supplied by the channel programs.  Absolute accesses imply bank number
   zero.

   Several auxiliary functions provide memory initialization, filling, and
   checking that a specified range of memory has not been used.  A full set of
   byte-access routines is provided to emulate byte addressing on the
   word-addressable HP 3000.

   The memory simulator provides the capability to trace memory reads and
   writes, as well as byte and BCD operands that are stored in memory.  Three
   general memory debug flags are defined and can be used by the other simulator
   modules to trace memory reads and writes, instruction fetches, and operand
   accesses.


   Implementation notes:

    1. Error detection and correction is not currently simulated.
*/



#include "hp3000_defs.h"
#include "hp3000_cpu.h"
#include "hp3000_mem.h"



/* Memory access classification table */

typedef struct {
    HP_WORD     *bank_ptr;                      /* a pointer to the bank register */
    uint32      debug_flag;                     /* the debug flag for tracing */
    const char  *name;                          /* the classification name */
    } ACCESS_PROPERTIES;


static const ACCESS_PROPERTIES mem_access [] = {        /* indexed by ACCESS_CLASS */
/*    bank_ptr  debug_flag   name                */
/*    --------  -----------  ------------------- */
    {  NULL,    DEB_MDATA,   "absolute"          },     /* absolute */
    {  NULL,    DEB_MDATA,   "absolute"          },     /* absolute_mapped */
    { & PBANK,  DEB_MFETCH,  "instruction fetch" },     /* fetch */
    { & PBANK,  DEB_MFETCH,  "instruction fetch" },     /* fetch_checked */
    { & PBANK,  DEB_MDATA,   "program"           },     /* program */
    { & PBANK,  DEB_MDATA,   "program"           },     /* program_checked */
    { & DBANK,  DEB_MDATA,   "data"              },     /* data */
    { & DBANK,  DEB_MDATA,   "data"              },     /* data_checked */
    { & DBANK,  DEB_MDATA,   "data"              },     /* data_mapped */
    { & DBANK,  DEB_MDATA,   "data"              },     /* data_mapped_checked */
    { & SBANK,  DEB_MDATA,   "stack"             },     /* stack */
    { & SBANK,  DEB_MDATA,   "stack"             },     /* stack_checked */
    {  NULL,    DEB_MDATA,   "dma"               }      /* dma */
    };


/* Memory local data structures */


/* Main memory */

static MEMORY_WORD *M = NULL;                           /* the pointer to the main memory allocation */



/* Memory global SCP helpers */


/* Examine a memory location.

   This routine is called by the SCP to examine memory.  The routine retrieves
   the memory location indicated by "address" as modified by any "switches" that
   were specified on the command line and returns the value in the first element
   of "eval_array".

   On entry, if "switches" includes SIM_SW_STOP, then "address" is an offset
   from PBANK; otherwise, it is an absolute address.  If the supplied address is
   beyond the current memory limit, "non-existent memory" status is returned.
   Otherwise, the value is obtained from memory and returned in "eval_array."
*/

t_stat mem_examine (t_value *eval_array, t_addr address, UNIT *uptr, int32 switches)
{
if (switches & SIM_SW_STOP)                             /* if entry is for a simulator stop */
    address = TO_PA (PBANK, address);                   /*   then form a PBANK-based physical address */

if (address >= MEMSIZE)                                 /* if the address is beyond memory limits */
    return SCPE_NXM;                                    /*   then return non-existent memory status */

else if (eval_array == NULL)                            /* if the value pointer was not supplied */
    return SCPE_IERR;                                   /*   then return internal error status */

else {                                                  /* otherwise */
    *eval_array = (t_value) M [address];                /*   store the return value */
    return SCPE_OK;                                     /*     and return success */
    }
}


/* Deposit to a memory location.

   This routine is called by the SCP to deposit to memory.  The routine stores
   the supplied "value" into memory at the "address" location.  If the supplied
   address is beyond the current memory limit, "non-existent memory" status is
   returned.

   The presence of any "switches" supplied on the command line does not affect
   the operation of the routine.
*/

t_stat mem_deposit (t_value value, t_addr address, UNIT *uptr, int32 switches)
{
if (address >= MEMSIZE)                                 /* if the address is beyond memory limits */
    return SCPE_NXM;                                    /*   then return non-existent memory status */

else {                                                  /* otherwise */
    M [address] = value & DV_MASK;                      /*   store the supplied value into memory */
    return SCPE_OK;                                     /*     and return success */
    }
}



/* Memory global routines */


/* Initialize main memory.

   The array of MEMORY_WORDs that represent the main memory of the HP 3000
   system is allocated and initialized to zero if the global pointer M has not
   been set.  The number of words to be allocated is supplied.  The routine
   returns TRUE if the allocation was successful or memory had already been
   allocated earlier, or FALSE if the allocation failed.
*/

t_bool mem_initialize (uint32 memory_size)
{
if (M == NULL)                                          /* if memory has not been allocated */
    M = (MEMORY_WORD *) calloc (memory_size,            /*   then allocate the maximum amount of memory needed */
                                sizeof (MEMORY_WORD));

return (M != NULL);
}


/* Check for non-zero value in a memory address range.

   A range of memory locations is checked for the presence of a non-zero value.
   The starting address of the range is supplied, and the check continues
   through the end of defined memory.  The routine returns TRUE if the memory
   range was empty (i.e., contained only zero values) and FALSE otherwise.
*/

t_bool mem_is_empty (uint32 starting_address)
{
uint32 address;

for (address = starting_address; address < MEMSIZE; address++)  /* loop through the specified address range */
    if (M [address] != NOP)                                     /* if this location is non-zero */
        return FALSE;                                           /*   then indicate that memory is not empty */

return TRUE;                                            /* return TRUE if all locations contain zero values */
}


/* Fill a range of memory with a value.

   Main memory locations from a supplied starting address through the end of
   defined memory are filled with the specified value.  This routine is
   typically used by the cold-load routine to fill memory with HALT 10
   instructions.
*/

void mem_fill (uint32 starting_address, HP_WORD fill_value)
{
uint32 address;

for (address = starting_address; address < MEMSIZE; address++)  /* loop through the specified address range */
    M [address] = (MEMORY_WORD) fill_value;                     /*   filling locations with the supplied value */

return;
}


/* Read a word from memory.

   Read and return a word from memory at the indicated offset and implied bank.
   If the access succeeds, the routine returns TRUE.  If the accessed word is
   outside of physical memory, the Illegal Address interrupt flag is set for
   CPU accesses, the value is set to 0, and the routine returns FALSE.  If
   access checking is requested, and the check fails, a Bounds Violation trap is
   taken.

   On entry, "dptr" points to the DEVICE structure of the device requesting
   access, "classification" is the type of access requested, "offset" is a
   logical offset into the memory bank implied by the access classification,
   except for absolute and DMA accesses, for which "offset" is a physical
   address, and "value" points to the variable to receive the memory content.

   Memory accesses other than DMA accesses may be checked or unchecked.  Checked
   program, data, and stack accesses must specify locations within the
   corresponding segments (PB <= ea <= PL for program, or DL <= ea <= S for data
   or stack) unless the CPU in is privileged mode, and those that reference the
   TOS locations return values from the TOS registers instead of memory.
   Checked absolute accesses return TOS location values if referenced but
   otherwise access memory directly with no additional restrictions.

   For data and stack accesses, there are three cases, depending on the
   effective address:

     - EA >= DL and EA <= SM : read from memory

     - EA > SM and EA <= SM + SR : read from a TOS register if bank = stack bank

     - EA < DL or EA > SM + SR : trap if not privileged, else read from memory


   Implementation notes:

    1. The physical address is formed by merging the bank and offset without
       masking either value to their respective register sizes.  Masking is not
       necessary, as it was done when the bank registers were loaded, and it is
       faster to avoid it.  Primarily, though, it is not done so that an invalid
       bank register value (e.g., loaded from a corrupted stack) will generate
       an illegal address interrupt and so will pinpoint the problem for
       debugging.

    2. In hardware, bounds checking is performed explicitly by microcode.  In
       simulation, bounds checking is performed explicitly by employing the
       "_checked" versions of the desired access classifications.
*/

t_bool mem_read (DEVICE *dptr, ACCESS_CLASS classification, uint32 offset, HP_WORD *value)
{
uint32 bank, address;

if (mem_access [classification].bank_ptr == NULL) {     /* if this is an absolute or DMA access */
    address = offset;                                   /*   then the "offset" is already a physical address */
    bank = TO_BANK (offset);                            /* separate the bank and offset */
    offset = TO_OFFSET (offset);                        /*   in case tracing is active */
    }

else {                                                  /* otherwise the bank register is implied */
    bank = *mem_access [classification].bank_ptr;       /*   by the access classification */
    address = bank << LA_WIDTH | offset;                /* form the physical address with the supplied offset */
    }

if (address >= MEMSIZE) {                               /* if this access is beyond the memory size */
    if (dptr == &cpu_dev)                               /*   then if an interrupt is requested */
        CPX1 |= cpx1_ILLADDR;                           /*     then set the Illegal Address interrupt */

    *value = 0;                                         /* return a zero value */
    return FALSE;                                       /*   and indicate failure to the caller */
    }

else {                                                  /* otherwise the access is within the memory range */
    switch (classification) {                           /*   so dispatch on the access classification */

        case dma:
        case absolute:
        case fetch:
        case program:
        case data:
            *value = (HP_WORD) M [address];             /* unchecked access values come from memory */
            break;


        case absolute_mapped:
        case data_mapped:
        case stack:
            if (offset > SM && offset <= SM + SR && bank == SBANK)  /* if the offset is within the TOS */
                *value = TR [SM + SR - offset];                     /*   then the value comes from a TOS register */
            else                                                    /* otherwise */
                *value = (HP_WORD) M [address];                     /*   the value comes from memory */
            break;


        case fetch_checked:
            if (PB <= offset && offset <= PL)           /* if the offset is within the program segment bounds */
                *value = (HP_WORD) M [address];         /*   then the value comes from memory */
            else                                        /* otherwise */
                MICRO_ABORT (trap_Bounds_Violation);    /*   trap for a bounds violation */
            break;


        case program_checked:
            if (PB <= offset && offset <= PL || PRIV)   /* if the offset is within bounds or is privileged */
                *value = (HP_WORD) M [address];         /*   then the value comes from memory */
            else                                        /* otherwise */
                MICRO_ABORT (trap_Bounds_Violation);    /*   trap for a bounds violation */
            break;


        case data_checked:
            if (DL <= offset && offset <= SM + SR || PRIV)  /* if the offset is within bounds or is privileged */
                *value = (HP_WORD) M [address];             /*   then the value comes from memory */
            else                                            /* otherwise */
                MICRO_ABORT (trap_Bounds_Violation);        /*   trap for a bounds violation */
            break;


        case data_mapped_checked:
        case stack_checked:
            if (offset > SM && offset <= SM + SR && bank == SBANK)  /* if the offset is within the TOS */
                *value = TR [SM + SR - offset];                     /*   then the value comes from a TOS register */
            else if (DL <= offset && offset <= SM + SR || PRIV)     /* if the offset is within bounds or is privileged */
                *value = (HP_WORD) M [address];                     /*   then the value comes from memory */
            else                                                    /* otherwise */
                MICRO_ABORT (trap_Bounds_Violation);                /*   trap for a bounds violation */
            break;
        }                                               /* all cases are handled */

    tpprintf (dptr, mem_access [classification].debug_flag,
              BOV_FORMAT "  %s%s\n", bank, offset, *value,
              mem_access [classification].name,
              mem_access [classification].debug_flag == DEB_MDATA ? " read" : "");

    return TRUE;                                        /* indicate success with the returned value stored */
    }
}


/* Write a word to memory.

   Write a word to memory at the indicated offset and implied bank.  If the
   write succeeds, the routine returns TRUE.  If the accessed location is outside
   of physical memory, the Illegal Address interrupt flag is set for CPU
   accesses, the write is ignored, and the routine returns FALSE.  If access
   checking is requested, and the check fails, a Bounds Violation trap is taken.

   For data and stack accesses, there are three cases, depending on the
   effective address:

     - EA >= DL and EA <= SM + SR : write to memory

     - EA > SM and EA <= SM + SR : write to a TOS register if bank = stack bank

     - EA < DL or EA > SM + SR : trap if not privileged, else write to memory

   Note that cases 1 and 2 together imply that a write to a TOS register also
   writes through to the underlying memory.


   Implementation notes:

    1. The physical address is formed by merging the bank and offset without
       masking either value to their respective register sizes.  Masking is not
       necessary, as it was done when the bank registers were loaded, and it is
       faster to avoid it.  Primarily, though, it is not done so that an invalid
       bank register value (e.g., loaded from a corrupted stack) will generate
       an illegal address interrupt and so will pinpoint the problem for
       debugging.

    2. In hardware, bounds checking is performed explicitly by microcode.  In
       simulation, bounds checking is performed explicitly by employing the
       "_checked" versions of the desired access classifications.

    3. The Series II microcode shows that only the STOR and STD instructions
       write through to memory when the effective address is in a TOS register.
       However, in simulation, all (checked) stack and data writes will write
       through.
*/

t_bool mem_write (DEVICE *dptr, ACCESS_CLASS classification, uint32 offset, HP_WORD value)
{
uint32 bank, address;

if (mem_access [classification].bank_ptr == NULL) {     /* if this is an absolute or DMA access */
    address = offset;                                   /*   then "offset" is already a physical address */
    bank = TO_BANK (offset);                            /* separate the bank and offset */
    offset = TO_OFFSET (offset);                        /*   in case tracing is active */
    }

else {                                                  /* otherwise the bank register is implied */
    bank = *mem_access [classification].bank_ptr;       /*    by the access classification */
    address = bank << LA_WIDTH | offset;                /* form the physical address with the supplied offset */
    }

if (address >= MEMSIZE) {                               /* if this access is beyond the memory size */
    if (dptr == &cpu_dev)                               /*   then if an interrupt is requested */
        CPX1 |= cpx1_ILLADDR;                           /*     then set the Illegal Address interrupt */

    return FALSE;                                       /* indicate failure to the caller */
    }

else {                                                  /* otherwise the access is within the memory range */
    switch (classification) {                           /*   so dispatch on the access classification */

        case dma:
        case absolute:
        case data:
            M [address] = (MEMORY_WORD) value;          /* write the value to memory */
            break;


        case absolute_mapped:
        case data_mapped:
        case stack:
            if (offset > SM && offset <= SM + SR && bank == SBANK)  /* if the offset is within the TOS */
                TR [SM + SR - offset] = value;                      /*   then write the value to a TOS register */
            else                                                    /* otherwise */
                M [address] = (MEMORY_WORD) value;                  /*   write the value to memory */
            break;


        case data_mapped_checked:
        case stack_checked:
            if (offset > SM && offset <= SM + SR && bank == SBANK)  /* if the offset is within the TOS */
                TR [SM + SR - offset] = value;                      /*   then write the value to a TOS register */

        /* fall through into checked cases */

        case data_checked:
            if (DL <= offset && offset <= SM + SR || PRIV)          /* if the offset is within bounds or is privileged */
                M [address] = (MEMORY_WORD) value;                  /*   then write the value to memory */
            else                                                    /* otherwise */
                MICRO_ABORT (trap_Bounds_Violation);                /*   trap for a bounds violation */
            break;


        case fetch:
        case fetch_checked:
        case program:
        case program_checked:                           /* these classes cannot be used for writing */
            CPX1 |= cpx1_ADDRPAR;                       /*   so set an Address Parity Error interrupt */
            return FALSE;                               /*     and indicate failure to the caller */

        }                                               /* all cases are handled */

    tpprintf (dptr, mem_access [classification].debug_flag,
              BOV_FORMAT "  %s write\n", bank, offset, value,
              mem_access [classification].name);

    return TRUE;                                        /* indicate success with the value written */
    }
}


/* Initialize a byte accessor.

   The supplied byte accessor structure is initialized for the starting relative
   byte offset pointer and type of access indicated.  If the supplied block
   length is non-zero and checked accesses are requested, then the starting and
   ending word addresses are bounds-checked, and a Bounds Violation will occur
   if the address range exceeds that permitted by the access.  If the block
   length is zero and checked accesses are requested, then only the starting
   address is checked, and it is the caller's responsibility to check additional
   accesses as they occur.

   The byte access routines assume that if the initial range or starting address
   is checked, succeeding accesses need not be checked, and vice versa.  The
   implication is that if the access class passed to this routine is checked,
   the routine might abort with a Bounds Violation, but succeeding read or write
   accesses will not, and if the class is unchecked, this routine will not abort
   but a succeeding access might.

   On return, the byte accessor is ready for use with the other byte access
   routines.


   Implementation notes:

    1. Calling mem_set_byte with the initial_byte_address field set to zero
       indicates an initialization call that should use the count field as the
       block length.  Zero is not a valid value for initial_byte_address, as
       memory location 0 is reserved for the code segment table pointer.
*/

void mem_init_byte (BYTE_ACCESS *bap, ACCESS_CLASS class, HP_WORD *byte_offset, uint32 block_length)
{
bap->class = INVERT_CHECK (class);                      /* invert the access check for succeeding calls */
bap->write_needed = FALSE;                              /*   and clear the word buffer occupation flag */

bap->byte_offset = byte_offset;                         /* save the pointer to the relative byte offset variable */
bap->first_byte_offset = *byte_offset;                  /*   and initialize the lowest byte offset */

bap->length = block_length;                             /* set the maximum extent length to the block length */
bap->count = block_length;                              /*   and pass the initial block length */
bap->initial_byte_address = 0;                          /*     in an initialization call */

mem_set_byte (bap);                                     /* set up the access from the initial byte offset */

bap->initial_word_address = bap->word_address;          /* save the starting word address */

bap->first_byte_address = bap->initial_byte_address;    /* save the lowest byte address */
bap->count = 0;                                         /*   and clear the byte access count */

return;
}


/* Set a byte accessor.

   The supplied byte accessor is set to access the updated address specified by
   the byte offset variable.  If the variable is altered directly, this routine
   must be called before calling any of the other byte access routines.  It is
   also called to update the first byte offset and length in preparation for
   formatting an operand for tracing.

   On return, the byte accessor is ready for use with the other byte access
   routines.


   Implementation notes:

    1. Entry with the initial_byte_address field set to zero indicates an
       initialization call; the count field will contain the block length.
       Entry with initial_byte_address non-zero indicates that the count field
       contains the number of bytes read or written since initialization.

    2. The operand extents are updated only if an access was made with the
       current accessor.  This avoids extending the bounds if the accessor was
       set but never used to read or write a byte.

    3. The class field contains the access class used when reading or writing
       bytes.  The initial access check uses the opposite sense.
*/

void mem_set_byte (BYTE_ACCESS *bap)
{
uint32 bank;

mem_update_byte (bap);                                  /* flush the last byte if written */

if (bap->count > 0 && bap->initial_byte_address > 0) {          /* if bytes have been accessed */
    if (bap->initial_byte_address < bap->first_byte_address) {  /*   then if the current address is lower */
        bap->length = bap->length + bap->first_byte_address     /*     then extend the length */
                                  - bap->initial_byte_address;  /*       by the additional amount */

        bap->first_byte_address = bap->initial_byte_address;    /* reset the lowest address seen */
        bap->first_byte_offset = bap->initial_byte_offset;      /*   and the lowest offset seen */
        }

    else                                                        /* otherwise the current address is higher */
        bap->count = bap->count + bap->initial_byte_address     /*   (or unchanged) so extend the count */
                                - bap->first_byte_address;      /*     by the additional amount if any */

    if (bap->length < bap->count)                       /* if the maximum length is less than the current count */
        bap->length = bap->count;                       /*   then reset the maximum to the current extent */

    bap->count = 0;                                     /* clear the access count */
    }

bap->initial_byte_offset = *bap->byte_offset;           /* set the new starting relative byte offset */

bap->word_address = cpu_byte_ea (INVERT_CHECK (bap->class), /* convert the new byte offset to a word address */
                                 *bap->byte_offset,         /*   and check the bounds if originally requested */
                                 bap->count);

if (mem_access [bap->class].bank_ptr == NULL)           /* if this is an absolute or DMA access */
    bank = 0;                                           /*   then the byte offset is already a physical address */
else                                                    /* otherwise */
    bank = *mem_access [bap->class].bank_ptr;           /*   the bank register is implied by the classification */

bap->initial_byte_address = TO_PA (bank, bap->word_address) * 2 /* save the physical starting byte address */
                              + (bap->initial_byte_offset & 1);

if ((bap->initial_byte_offset & 1) == 0)                    /* if the starting byte offset is even */
    bap->word_address = bap->word_address - 1 & LA_MASK;    /*   then bias the address for the first read */

return;
}


/* Reset a byte accessor.

   The supplied byte accessor is reset to access the original address specified
   in the "mem_init_byte" call.  It is used to "rewind" a byte accessor, e.g.,
   in preparation to reread the bytes or to rewrite after reading the bytes.

   The routine does not alter the address and offset of the lowest byte
   accessed, so these values are retained across a reset.

   On return, the byte accessor is ready for use with the other byte access
   routines.
*/

void mem_reset_byte (BYTE_ACCESS *bap)
{
mem_update_byte (bap);                                  /* flush the last byte if written */

*bap->byte_offset = bap->initial_byte_offset;           /* restore the original byte offset */
bap->word_address = bap->initial_word_address;          /*   and word address */

bap->count = 0;                                         /* clear the byte access count */

return;
}


/* Look up a byte in a table.

   The byte located in the table designated by the byte accessor pointer "bap"
   at the entry designated by the "index" parameter is returned.  The table is
   byte-addressable and assumed to be long enough to contain the indexed
   entry.


   Implementation notes:

    1. Successive lookups using the same index incur only one memory read
       penalty.
*/

HP_BYTE mem_lookup_byte (BYTE_ACCESS *bap, uint8 index)
{
uint32 byte_offset, word_address;

byte_offset = *bap->byte_offset + (HP_WORD) index       /* get the offset to the indexed location */
                & LA_MASK;

word_address = cpu_byte_ea (bap->class, byte_offset, 0);    /* convert to a word address and check the bounds */

if (word_address != bap->word_address) {                /* if the address is not the same as the prior access */
    bap->word_address = word_address;                   /*   then set the new address */
    cpu_read_memory (bap->class, word_address,          /*     and read the memory word */
                     &bap->data_word);                  /*       containing the target byte */
    }

if (byte_offset & 1)                                    /* if the byte offset is odd */
    return LOWER_BYTE (bap->data_word);                 /*   then return the lower byte */
else                                                    /* otherwise */
    return UPPER_BYTE (bap->data_word);                 /*   return the upper byte */
}


/* Read the next byte.

   The next byte indicated by the supplied byte accessor is returned.

   If a new memory word must be read, and a previous byte write has not written
   the buffered word into memory, it is posted.  Then the next word is read from
   memory, and the indicated byte is returned.


   Implementation notes:

    1. The data_word field is not read until the first access is made.  This
       ensures that a Bounds Violation does not occur on an unchecked
       initialization call but instead occurs when the byte is actually
       accessed.
*/

HP_BYTE mem_read_byte (BYTE_ACCESS *bap)
{
HP_BYTE byte;

if (*bap->byte_offset & 1) {                            /* if the byte offset is odd */
    if (bap->count == 0)                                /*   then if this is the first access */
        cpu_read_memory (bap->class, bap->word_address, /*     then read the data word */
                         &bap->data_word);              /*       containing the target byte */

    byte = LOWER_BYTE (bap->data_word);                 /* get the lower byte */
    }

else {                                                      /* otherwise */
    if (bap->write_needed) {                                /*   if the buffer is occupied */
        bap->write_needed = FALSE;                          /*     then mark it written */
        cpu_write_memory (bap->class, bap->word_address,    /*        and write the word back */
                          bap->data_word);
        }

    bap->word_address = bap->word_address + 1 & LA_MASK;    /* update the word address */
    cpu_read_memory (bap->class, bap->word_address,         /* read the data word */
                     &bap->data_word);                      /*   containing the target byte */
    byte = UPPER_BYTE (bap->data_word);                     /*     and get the upper byte */
    }

*bap->byte_offset = *bap->byte_offset + 1 & LA_MASK;    /* update the byte offset */
bap->count = bap->count + 1;                            /*   and the access count */

return byte;
}


/* Write the next byte.

   The next byte indicated by the supplied byte accessor is written.  If the
   lower byte is accessed, the containing word is written to memory, and the
   buffer word is marked vacant.  Otherwise, the upper byte is placed in the
   buffer word, and the flag is set to indicate that the word will need to be
   written to memory.


   Implementation notes:

    1. The data_word field is not read until the first access is made.  This
       ensures that a Bounds Violation does not occur on an unchecked
       initialization call but instead occurs when the byte is actually
       accessed.
*/

void mem_write_byte (BYTE_ACCESS *bap, HP_BYTE byte)
{
if (*bap->byte_offset & 1) {                                /* if the byte offset is odd */
    if (bap->count == 0)                                    /*   then if this is the first access */
        cpu_read_memory (bap->class, bap->word_address,     /*     then read the data word */
                         &bap->data_word);                  /*       containing the target byte */

    bap->data_word = REPLACE_LOWER (bap->data_word, byte);  /* replace the lower byte */
    cpu_write_memory (bap->class, bap->word_address,        /*   and write the word to memory */
                      bap->data_word);
    bap->write_needed = FALSE;                              /* clear the occupancy flag */
    }

else {                                                      /* otherwise the offset is even */
    bap->word_address = bap->word_address + 1 & LA_MASK;    /*   so update the word address */
    bap->data_word = REPLACE_UPPER (bap->data_word, byte);  /* replace the upper byte */
    bap->write_needed = TRUE;                               /*   and set the occupancy flag */
    }

*bap->byte_offset = *bap->byte_offset + 1 & LA_MASK;        /* update the byte offset */
bap->count = bap->count + 1;                                /*   and the access count */

return;
}


/* Modify the last byte accessed.

   The last byte read or written as indicated by the supplied byte accessor is
   modified in-place with the new value supplied.  The current byte offset will
   be odd if the last byte accessed was the upper (even) byte, or it will be
   even if the last byte accessed was the lower (odd) byte.  The current byte
   offset is not changed by this routine.
*/

void mem_modify_byte (BYTE_ACCESS *bap, HP_BYTE byte)
{
if (*bap->byte_offset & 1) {                                /* if the last byte offset was even */
    bap->data_word = REPLACE_UPPER (bap->data_word, byte);  /*   then replace the upper byte */
    bap->write_needed = TRUE;                               /*     and set the occupancy flag */
    }

else {                                                      /* otherwise the last offset was odd */
    bap->data_word = REPLACE_LOWER (bap->data_word, byte);  /*   so replace the lower byte */
    cpu_write_memory (bap->class, bap->word_address,        /* write the word back */
                      bap->data_word);
    bap->write_needed = FALSE;                              /* clear the occupancy flag */
    }

return;
}


/* Post the current buffer word.

   The buffer word held by the supplied byte accessor is written to memory if
   the occupancy flag is set.  Otherwise, no action is taken.

   This routine must be called to terminate any sequence of byte operations
   that involves calls to mem_read_byte and mem_modify_byte.  It ensures that
   the final byte written is flushed to memory.


   Implementation notes:

    1. Because a preceding mem_read_byte call has been made, the data_word field
       already contains the byte that was NOT modified, so a read-modify-write
       access is not needed.
*/

void mem_post_byte (BYTE_ACCESS *bap)
{
if (bap->write_needed) {                                /* if the buffer needs to be written */
    bap->write_needed = FALSE;                          /*   then clear the occupancy flag */
    cpu_write_memory (bap->class, bap->word_address,    /*     and write the word to memory */
                      bap->data_word);
    }

return;
}


/* Rewrite the current buffer word.

   The upper byte of the buffer word held by the supplied byte accessor replaces
   the upper byte of the current memory word without disturbing the lower byte,
   and the word is rewritten to memory if the occupancy flag is set.  Otherwise,
   no action is taken.

   This routine should be called to terminate any sequence of byte operations
   that involves calls to mem_write_byte.  It ensures that the final byte
   written is flushed to memory.  The read-modify-write sequence ensures that
   the existing lower byte in the memory word is retained.
*/

void mem_update_byte (BYTE_ACCESS *bap)
{
HP_WORD target_word;

if (bap->write_needed) {                                /* if the buffer needs to be written */
    bap->write_needed = FALSE;                          /*   then clear the occupancy flag */

    cpu_read_memory (bap->class, bap->word_address, &target_word);      /* read the data word */
    bap->data_word = REPLACE_LOWER (bap->data_word, target_word);       /*   and replace the lower byte */
    cpu_write_memory (bap->class, bap->word_address, bap->data_word);   /*     and write the word back */
    }

return;
}


/* Format a byte operand.

   The byte string starting at the absolute byte address given by the
   "byte_address" parameter and of "byte_count" bytes in length is copied into
   a local character buffer and terminated by a NUL character.  A pointer to the
   buffer is returned.

   No translation of non-printable characters is performed, so if the caller
   interprets the returned formatted operand as a character string, an embedded
   NUL will truncate the string.


   Implementation notes:

    1. This routine accesses the memory array directly to avoid tracing the
       memory reads if debug tracing is enabled.

    2. The byte count is assumed to be 256 or less for convenience.
*/

char *fmt_byte_operand (uint32 byte_address, uint32 byte_count)
{
static char buffer [257];
char        *cptr;
uint32      address;

if (byte_count > 256)                                   /* truncate the formatted operand */
    byte_count = 256;                                   /*   if it's too long */

address = byte_address / 2;                             /* convert to an absolute word address */

cptr = buffer;                                          /* point at the start of the buffer */

while (byte_count-- > 0)                                /* while there are bytes to transfer */
    if (byte_address++ & 1)                             /* if the byte address is odd */
        *cptr++ = LOWER_BYTE (M [address++]);           /*   then copy the lower byte and bump the word address */
    else if (address < MEMSIZE)                         /* otherwise if the word address is valid */
        *cptr++ = UPPER_BYTE (M [address]);             /*   then copy the upper byte */
    else                                                /* otherwise the address is beyond the end of memory */
        break;                                          /*   so terminate the operand at this point */

*cptr = '\0';                                           /* add a trailing NUL */

return buffer;                                          /* return a pointer to the formatted operand */
}


/* Format a translated byte operand.

   The byte string starting at the absolute byte address given by the
   "byte_address" parameter and of "byte_count" bytes in length is formatted
   into a NUL-terminated character string and then translated using the lookup
   table given by the "table_address" parameter.  A pointer to the string is
   returned.


   Implementation notes:

    1. This routine accesses the memory array directly to avoid tracing the
       memory reads if debug tracing is enabled.

    2. The routine will not return a string longer than 256 characters.
*/

char *fmt_translated_byte_operand (uint32 byte_address, uint32 byte_count, uint32 table_address)
{
char   *bptr, *cptr;
uint32 index;

bptr = fmt_byte_operand (byte_address, byte_count);     /* format the byte string */

cptr = bptr;                                            /* point at the start of the buffer */

while (byte_count-- > 0) {                              /* while there are bytes to translate */
    index = table_address + *cptr;                      /*   index into the translation table */

    if (index & 1)                                      /* if the translated byte address is odd */
        *cptr++ = LOWER_BYTE (M [index / 2]);           /*   then copy the lower byte from the table */
    else                                                /* otherwise */
        *cptr++ = UPPER_BYTE (M [index / 2]);           /*   copy the upper byte from the table */
    }

return bptr;                                            /* return a pointer to the translated operand */
}


/* Format a BCD operand.

   The BCD numeric string starting at the absolute byte address given by the
   "byte_address" parameter and of "digit_count" BCD digits in length is
   formatted into a NUL-terminated character string and then reformatted into a
   local buffer as a hexadecimal character string.  A pointer to the buffer is
   returned.

   The digit count does not include the numeric sign, located in the four bits
   following the last digit.  If the digit count is even, the left-half of the
   first byte is unused, as BCD strings always end in the right-half of the last
   byte.


   Implementation notes:

    1. The digit count is assumed to be 32 or less, as HP 3000 BCD ("packed
       decimal") numbers may not contain more than 28 digits.
*/


char *fmt_bcd_operand (uint32 byte_address, uint32 digit_count)
{
static char hex [] = "0123456789ABCDEF";
static char buffer [33];
uint32      byte_count;
char        *bptr, *cptr;

if (digit_count > 32)                                   /* if the operand is too long */
    return "(invalid)";                                 /*   then return an error indication */

byte_count = digit_count / 2 + 1;                       /* convert from a digit to a byte count */
bptr = fmt_byte_operand (byte_address, byte_count);     /*   and format the byte string */

cptr = buffer;                                          /* point at the start of the buffer */

if ((digit_count & 1) == 0) {                           /* if the digit count is even */
    *cptr++ = hex [LOWER_HALF (*bptr++)];               /*   then the BCD string starts with */
    byte_count--;                                       /*     the lower half of the first byte */
    }

while (byte_count-- > 0) {                              /* while there are digits to format */
    *cptr++ = hex [UPPER_HALF (*bptr)];                 /* format and copy the digit in the upper half */
    *cptr++ = hex [LOWER_HALF (*bptr++)];               /*   followed by the digit in the lower half */
    }

*cptr = '\0';                                           /* add a trailing NUL */

return buffer;                                          /* return a pointer to the formatted operand */
}
