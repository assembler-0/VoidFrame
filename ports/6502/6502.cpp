#include "Console.h"

// Helper types
using Byte = unsigned char;
using Word = unsigned short;
using u32 = unsigned int;

/// @brief Memory model for the 6502
struct MEM {
    static constexpr u32 MAX_MEM = 1024 * 64;
    Byte Data[MAX_MEM];

    void Init(){
        for (unsigned char & i : Data){
            i = 0;
        }
    }

    // Read a byte from memory
    Byte operator[](const u32 Address) const {
        // Assert Address is < MAX_MEM
        return Data[Address & 0xFFFF];
    }
    // Write a byte to memory
    Byte& operator[](const u32 Address) {
        // Assert Address is < MAX_MEM
        return Data[Address & 0xFFFF];
    }
    // Write a byte and decrement cycles
    void WriteByte(Byte Value, u32 Address, u32& Cycles) {
        Data[Address & 0xFFFF] = Value;
        Cycles--;
    }
};

/// @brief CPU model for the 6502
struct CPU {

    Word PC; // Program Counter
    Byte SP; // Stack Pointer

    Byte A, X, Y; // Registers

    // Status Flags
    Byte C : 1; // Carry
    Byte Z : 1; // Zero
    Byte I : 1; // Interrupt Disable
    Byte D : 1; // Decimal Mode
    Byte B : 1; // Break Command
    Byte V : 1; // Overflow
    Byte N : 1; // Negative

    /// @brief Resets the CPU to a known state
    void Reset(MEM& Memory){
        SP = 0xFF;
        C = Z = I = D = V = B = N = 0;
        A = X = Y = 0;
        PC = static_cast<Word>(Memory[0xFFFC]) | (static_cast<Word>(Memory[0xFFFD]) << 8);
    }

    // Set Zero and Negative flags based on a value
    void SetZN(Byte value) {
        Z = (value == 0);
        N = (value & 0b10000000) > 0;
    }

    // Fetches a byte from memory, increments PC, and decrements cycles
    Byte FetchByte(u32& Cycles, MEM& Memory){
        Byte Data = Memory[PC];
        PC++;
        Cycles--;
        return Data;
    }

    // Fetches a word from memory, increments PC, and decrements cycles
    Word FetchWord(u32& Cycles, MEM& Memory){
        Word Data = Memory[PC];
        PC++;
        Data |= (Memory[PC] << 8);
        PC++;
        Cycles -= 2;
        return Data;
    }

    // Reads a byte from a given address and decrements cycles
    static Byte ReadByte(u32& Cycles, Word Address, MEM& Memory) {
        Byte Data = Memory[Address];
        Cycles--;
        return Data;
    }

    // Addressing modes
    Word Addr_IM(u32& Cycles, MEM& Memory) { return PC++; }
    Word Addr_ZP(u32& Cycles, MEM& Memory) { return FetchByte(Cycles, Memory); }
    Word Addr_ZPX(u32& Cycles, MEM& Memory) {
        Byte ZPAddress = FetchByte(Cycles, Memory);
        ZPAddress += X;
        Cycles--;
        return ZPAddress;
    }
    Word Addr_ZPY(u32& Cycles, MEM& Memory) {
        Byte ZPAddress = FetchByte(Cycles, Memory);
        ZPAddress += Y;
        Cycles--;
        return ZPAddress;
    }
    Word Addr_ABS(u32& Cycles, MEM& Memory) { return FetchWord(Cycles, Memory); }
    Word Addr_ABSX(u32& Cycles, MEM& Memory) {
        Word AbsAddress = FetchWord(Cycles, Memory);
        if ((AbsAddress & 0xFF00) != ((AbsAddress + X) & 0xFF00)) {
            Cycles--; // Page boundary crossing
        }
        return AbsAddress + X;
    }
    Word Addr_ABSY(u32& Cycles, MEM& Memory) {
        Word AbsAddress = FetchWord(Cycles, Memory);
        if ((AbsAddress & 0xFF00) != ((AbsAddress + Y) & 0xFF00)) {
            Cycles--; // Page boundary crossing
        }
        return AbsAddress + Y;
    }
    Word Addr_INDX(u32& Cycles, MEM& Memory) {
        Byte ZPAddress = FetchByte(Cycles, Memory);
        ZPAddress += X;
        Cycles--;
        Word EffAddress = ReadByte(Cycles, ZPAddress, Memory);
        EffAddress |= (ReadByte(Cycles, (Byte)(ZPAddress + 1), Memory) << 8);
        return EffAddress;
    }
    Word Addr_INDY(u32& Cycles, MEM& Memory) {
        Byte ZPAddress = FetchByte(Cycles, Memory);
        Word EffAddress = ReadByte(Cycles, ZPAddress, Memory);
        EffAddress |= (ReadByte(Cycles, (Byte)(ZPAddress + 1), Memory) << 8);
        if ((EffAddress & 0xFF00) != ((EffAddress + Y) & 0xFF00)) {
            Cycles--; // Page boundary crossing
        }
        return EffAddress + Y;
    }


    // Opcodes
    static constexpr Byte INS_LDA_IM = 0xA9;
    static constexpr Byte INS_LDA_ZP = 0xA5;
    static constexpr Byte INS_LDA_ZPX = 0xB5;
    static constexpr Byte INS_LDA_ABS = 0xAD;
    static constexpr Byte INS_LDA_ABSX = 0xBD;
    static constexpr Byte INS_LDA_ABSY = 0xB9;
    static constexpr Byte INS_LDA_INDX = 0xA1;
    static constexpr Byte INS_LDA_INDY = 0xB1;

    static constexpr Byte INS_LDX_IM = 0xA2;
    static constexpr Byte INS_LDX_ZP = 0xA6;
    static constexpr Byte INS_LDX_ZPY = 0xB6;
    static constexpr Byte INS_LDX_ABS = 0xAE;
    static constexpr Byte INS_LDX_ABSY = 0xBE;

    static constexpr Byte INS_LDY_IM = 0xA0;
    static constexpr Byte INS_LDY_ZP = 0xA4;
    static constexpr Byte INS_LDY_ZPX = 0xB4;
    static constexpr Byte INS_LDY_ABS = 0xAC;
    static constexpr Byte INS_LDY_ABSX = 0xBC;

    static constexpr Byte INS_STA_ZP = 0x85;
    static constexpr Byte INS_STA_ZPX = 0x95;
    static constexpr Byte INS_STA_ABS = 0x8D;
    static constexpr Byte INS_STA_ABSX = 0x9D;
    static constexpr Byte INS_STA_ABSY = 0x99;
    static constexpr Byte INS_STA_INDX = 0x81;
    static constexpr Byte INS_STA_INDY = 0x91;

    static constexpr Byte INS_STX_ZP = 0x86;
    static constexpr Byte INS_STX_ZPY = 0x96;
    static constexpr Byte INS_STX_ABS = 0x8E;

    static constexpr Byte INS_STY_ZP = 0x84;
    static constexpr Byte INS_STY_ZPX = 0x94;
    static constexpr Byte INS_STY_ABS = 0x8C;

    static constexpr Byte INS_JSR = 0x20;
    static constexpr Byte INS_RTS = 0x60;
    static constexpr Byte INS_JMP_ABS = 0x4C;
    static constexpr Byte INS_JMP_IND = 0x6C;

    static constexpr Byte INS_PHA = 0x48;
    static constexpr Byte INS_PLA = 0x68;

    static constexpr Byte INS_BCC = 0x90;
    static constexpr Byte INS_BCS = 0xB0;
    static constexpr Byte INS_BEQ = 0xF0;
    static constexpr Byte INS_BNE = 0xD0;

    static constexpr Byte INS_CLC = 0x18;
    static constexpr Byte INS_SEC = 0x38;
    static constexpr Byte INS_NOP = 0xEA;
    static constexpr Byte INS_BRK = 0x00;

    static constexpr Byte INS_ADC_IM = 0x69;
    static constexpr Byte INS_SBC_IM = 0xE9;

    static constexpr Byte INS_CMP_IM = 0xC9;
    static constexpr Byte INS_CPX_IM = 0xE0;
    static constexpr Byte INS_CPY_IM = 0xC0;


    void ADC(Byte Operand) {
        Word Sum = A + Operand + C;
        C = (Sum > 0xFF);
        V = (~(A ^ Operand) & (A ^ (Byte)Sum)) & 0x80;
        A = (Byte)Sum;
        SetZN(A);
    }

    void SBC(Byte Operand) {
        ADC(~Operand);
    }

    void CMP(Byte Operand) {
        Byte Result = A - Operand;
        C = (A >= Operand);
        SetZN(Result);
    }

    void CPX(Byte Operand) {
        Byte Result = X - Operand;
        C = (X >= Operand);
        SetZN(Result);
    }

    void CPY(Byte Operand) {
        Byte Result = Y - Operand;
        C = (Y >= Operand);
        SetZN(Result);
    }

    void Execute(u32 Cycles, MEM& Memory){
        while (Cycles > 0){
            Byte Instruction = FetchByte(Cycles, Memory);
            switch (Instruction){
                // BRK
                case INS_BRK: {
                    Cycles = 0; // Halt
                } break;

                // Load Instructions
                case INS_LDA_IM: { A = FetchByte(Cycles, Memory); SetZN(A); } break;
                case INS_LDA_ZP: { Word Address = Addr_ZP(Cycles, Memory); A = ReadByte(Cycles, Address, Memory); SetZN(A); } break;
                case INS_LDA_ZPX: { Word Address = Addr_ZPX(Cycles, Memory); A = ReadByte(Cycles, Address, Memory); SetZN(A); } break;
                case INS_LDA_ABS: { Word Address = Addr_ABS(Cycles, Memory); A = ReadByte(Cycles, Address, Memory); SetZN(A); } break;
                case INS_LDA_ABSX: { Word Address = Addr_ABSX(Cycles, Memory); A = ReadByte(Cycles, Address, Memory); SetZN(A); } break;
                case INS_LDA_ABSY: { Word Address = Addr_ABSY(Cycles, Memory); A = ReadByte(Cycles, Address, Memory); SetZN(A); } break;
                case INS_LDA_INDX: { Word Address = Addr_INDX(Cycles, Memory); A = ReadByte(Cycles, Address, Memory); SetZN(A); } break;
                case INS_LDA_INDY: { Word Address = Addr_INDY(Cycles, Memory); A = ReadByte(Cycles, Address, Memory); SetZN(A); } break;
                case INS_LDX_IM: { X = FetchByte(Cycles, Memory); SetZN(X); } break;
                case INS_LDX_ZP: { Word Address = Addr_ZP(Cycles, Memory); X = ReadByte(Cycles, Address, Memory); SetZN(X); } break;
                case INS_LDY_IM: { Y = FetchByte(Cycles, Memory); SetZN(Y); } break;
                case INS_LDY_ZP: { Word Address = Addr_ZP(Cycles, Memory); Y = ReadByte(Cycles, Address, Memory); SetZN(Y); } break;

                // Store Instructions
                case INS_STA_ZP: { Word Address = Addr_ZP(Cycles, Memory); Memory.WriteByte(A, Address, Cycles); } break;
                case INS_STA_ZPX: { Word Address = Addr_ZPX(Cycles, Memory); Memory.WriteByte(A, Address, Cycles); } break;
                case INS_STA_ABS: { Word Address = Addr_ABS(Cycles, Memory); Memory.WriteByte(A, Address, Cycles); } break;
                case INS_STA_ABSX: { Word Address = Addr_ABSX(Cycles, Memory); Memory.WriteByte(A, Address, Cycles); } break;
                case INS_STA_ABSY: { Word Address = Addr_ABSY(Cycles, Memory); Memory.WriteByte(A, Address, Cycles); } break;
                case INS_STA_INDX: { Word Address = Addr_INDX(Cycles, Memory); Memory.WriteByte(A, Address, Cycles); } break;
                case INS_STA_INDY: { Word Address = Addr_INDY(Cycles, Memory); Memory.WriteByte(A, Address, Cycles); } break;
                case INS_STX_ZP: { Word Address = Addr_ZP(Cycles, Memory); Memory.WriteByte(X, Address, Cycles); } break;
                case INS_STY_ZP: { Word Address = Addr_ZP(Cycles, Memory); Memory.WriteByte(Y, Address, Cycles); } break;

                // Arithmetic
                case INS_ADC_IM: { Byte Val = FetchByte(Cycles, Memory); ADC(Val); } break;
                case INS_SBC_IM: { Byte Val = FetchByte(Cycles, Memory); SBC(Val); } break;

                // Comparisons
                case INS_CMP_IM: { Byte Val = FetchByte(Cycles, Memory); CMP(Val); } break;
                case INS_CPX_IM: { Byte Val = FetchByte(Cycles, Memory); CPX(Val); } break;
                case INS_CPY_IM: { Byte Val = FetchByte(Cycles, Memory); CPY(Val); } break;

                // Jumps and Subroutines
                case INS_JSR: {
                    Word SubroutineAddress = FetchWord(Cycles, Memory);
                    Memory.WriteByte((PC - 1) >> 8, 0x0100 + SP, Cycles);
                    SP--;
                    Memory.WriteByte((PC - 1) & 0xFF, 0x0100 + SP, Cycles);
                    SP--;
                    PC = SubroutineAddress;
                } break;
                case INS_RTS: {
                    SP++;
                    Word Lo = ReadByte(Cycles, 0x0100 + SP, Memory);
                    SP++;
                    Word Hi = ReadByte(Cycles, 0x0100 + SP, Memory);
                    PC = (Hi << 8) | Lo;
                    PC++;
                    Cycles--;
                } break;
                case INS_JMP_ABS: { PC = Addr_ABS(Cycles, Memory); } break;
                case INS_JMP_IND: {
                    Word Address = FetchWord(Cycles, Memory);
                    PC = ReadByte(Cycles, Address, Memory) | (ReadByte(Cycles, Address + 1, Memory) << 8);
                } break;

                // Stack operations
                case INS_PHA: { Memory.WriteByte(A, 0x0100 + SP, Cycles); SP--; } break;
                case INS_PLA: { SP++; A = ReadByte(Cycles, 0x0100 + SP, Memory); SetZN(A); } break;

                // Branching
                case INS_BCC: {
                    signed char Offset = (signed char)FetchByte(Cycles, Memory);
                    if (C == 0) {
                        Word OldPC = PC;
                        PC += Offset;
                        Cycles--;
                        if ((OldPC & 0xFF00) != (PC & 0xFF00)) Cycles--; // page crossing
                    }
                } break;
                case INS_BCS: {
                    signed char Offset = (signed char)FetchByte(Cycles, Memory);
                    if (C == 1) {
                        Word OldPC = PC;
                        PC += Offset;
                        Cycles--;
                        if ((OldPC & 0xFF00) != (PC & 0xFF00)) Cycles--;
                    }
                } break;
                case INS_BEQ: {
                    signed char Offset = (signed char)FetchByte(Cycles, Memory);
                    if (Z == 1) {
                        Word OldPC = PC;
                        PC += Offset;
                        Cycles--;
                        if ((OldPC & 0xFF00) != (PC & 0xFF00)) Cycles--;
                    }
                } break;
                case INS_BNE: {
                    signed char Offset = (signed char)FetchByte(Cycles, Memory);
                    if (Z == 0) {
                        Word OldPC = PC;
                        PC += Offset;
                        Cycles--;
                        if ((OldPC & 0xFF00) != (PC & 0xFF00)) Cycles--;
                    }
                } break;

                // Status Flag Changes
                case INS_CLC: { C = 0; Cycles--; } break;
                case INS_SEC: { C = 1; Cycles--; } break;

                // NOP
                case INS_NOP: { Cycles--; } break;

                default: {
                    PrintKernelF("Instruction not handled: %d\n", static_cast<int>(Instruction));
                    Cycles = 0; // Stop execution
                } break;
            }
        }
    }
};

extern "C" void Entry6502(const char * args){
    (void)args;
    CPU cpu{};
    static MEM mem;         // avoid large stack object
    mem.Init();             // clear memory explicitly
    // Write reset vector first
    mem[0xFFFC] = 0x00;
    mem[0xFFFD] = 0xF0;
    cpu.Reset(mem);
    // Load a program into memory
    int i = 0;
    mem[0xF000 + i++] = CPU::INS_CLC;       // Clear Carry
    mem[0xF000 + i++] = CPU::INS_LDA_IM;    // A = 10
    mem[0xF000 + i++] = 0x0A;
    mem[0xF000 + i++] = CPU::INS_ADC_IM;    // A = A + 5 + C -> A = 15
    mem[0xF000 + i++] = 0x05;
    mem[0xF000 + i++] = CPU::INS_STA_ZP;    // mem[0x10] = 15
    mem[0xF000 + i++] = 0x10;
    mem[0xF000 + i++] = CPU::INS_SEC;       // Set Carry
    mem[0xF000 + i++] = CPU::INS_SBC_IM;    // A = A - 2 - (1-C) -> A = 15 - 2 - 0 = 13
    mem[0xF000 + i++] = 0x02;
    mem[0xF000 + i++] = CPU::INS_CMP_IM;    // Compare A (13) with 13
    mem[0xF000 + i++] = 0x0D;               // Sets Z flag
    mem[0xF000 + i++] = CPU::INS_BEQ;       // Branch if Z is set
    mem[0xF000 + i++] = 0x02;               // Offset to next instruction
    mem[0xF000 + i++] = CPU::INS_LDA_IM;    // This should be skipped
    mem[0xF000 + i++] = 0xFF;
    mem[0xF000 + i++] = CPU::INS_LDX_IM;    // X = 42
    mem[0xF000 + i++] = 0x2A;
    mem[0xF000 + i++] = CPU::INS_BRK;       // Halt
    // PC already set by reset vector
    // Execute for a number of cycles
    cpu.Execute(50, mem);
    PrintKernelF("A: %d\n", static_cast<int>(cpu.A));
    PrintKernelF("X: %d\n", static_cast<int>(cpu.X));
    PrintKernelF("Value at 0x10: %d\n", static_cast<int>(mem[0x10]));
    PrintKernelF("Zero Flag: %d\n", static_cast<int>(cpu.Z));
    PrintKernelF("PC: %d\n", cpu.PC);
    PrintKernelSuccess("6502 emulation complete.\n");
}
