#include "cpudatasection.h"
CPUDataSection* CPUDataSection::_instance = nullptr;
CPUDataSection::CPUDataSection(QObject *parent): QObject(parent),cpuFeatures(Enu::OneByteDataBus),mainBusState(Enu::None),
    registerBank(32),memoryRegisters(6),memory(0XFFFF+1),controlSignals(20),clockSignals(10),hadDataError(false),errorMessage("")
{
    presetStaticRegisters();
}

bool CPUDataSection::aluFnIsUnary() const
{
    //The only alu functions that are unary are 0 & 10..15
    return controlSignals[Enu::ALU]==0||controlSignals[Enu::ALU]>=10;
}

bool CPUDataSection::getAMuxOutput(quint8& result) const
{
    //This function should not raise an error. Anything that needs an input from from AMUX will manually raise an error
    switch(cpuFeatures)
    {
    case Enu::OneByteDataBus:
        //For the one byte bus, either the value comes from the MDR register
        if(controlSignals[Enu::AMux]==0)
        {
            result = memoryRegisters[Enu::MEM_MDR];
            return true;
        }
        //Or it comes from the memory bank
        else if(controlSignals[Enu::AMux]==1)
        {
            return valueOnABus(result);
        }
        //Otherwise, AMUX is disabled and has no output
        else return false;
    case Enu::TwoByteDataBus:
        //For the two byte bus, the value either comes from EOMux
        if(controlSignals[Enu::AMux]==0)
        {
            //Which could come from MDRE when EOMux is 0
            if(controlSignals[Enu::EOMux]==0)
            {
                result=memoryRegisters[Enu::MEM_MDRE];
                return true;
            }
            //Or comes from MDRO if EOMux is 1
            else if(controlSignals[Enu::EOMux]==1)
            {
                result=memoryRegisters[Enu::MEM_MDRO];
                return true;
            }
            //Or has no has no output when EOMux is disabled
            else return false;

        }
        //Or the value of AMux comes from the register bank via A
        else if(controlSignals[Enu::AMux]==1)
        {
            return valueOnABus(result);
        }
        else return false;
        break;
    default:
        break;
    }
    //If control flow reaches here, it is safe to assume that AMux has no valid output
    return false;
}

bool CPUDataSection::calculateCSMuxOutput(bool &result) const
{
    //CSMux either outputs C when CS is 0
    if(controlSignals[Enu::CSMux]==0)
    {
        result=NZVCSbits&Enu::CMask;
        return true;
    }
    //Or outputs S when CS is 1
    else if(controlSignals[Enu::CSMux]==1)
    {
        result = NZVCSbits&Enu::SMask;
        return true;
    }
    //Otherwise it does not have valid output
    else return false;
}

bool CPUDataSection::calculatALUOutput(quint8 &res, quint8 &NZVC) const
{
    //This function should not set any errors.
    //Errors will be handled by step(..)
    quint8 a,b;
    bool carryIn;
    bool hasA=getAMuxOutput(a), hasB=valueOnBBus(b),hasCin=calculateCSMuxOutput(carryIn);
    if(!((aluFnIsUnary()&&hasA)||(hasA&&hasB)))
    {
        //The ALU output calculation would not be meaningful given its current function and inputs
        return false;
    }

    //Unless otherwise noted, do not return true (sucessfully) early, or the calculation for the NZ bits will be skipped
    switch(controlSignals[Enu::ALU])
    {
    case Enu::A_func: //A
        res=a;
        break;
    case Enu::ApB_func: //A plus B
        res=a+b;
        NZVC|= Enu::CMask*((int)(res<a||res<b)); //Carry out if result is unsigned less than a or b.
        //V Bit calculation could be wrong
        NZVC|= Enu::VMask*((((a & 0x7f) + (b & 0x7f)) >> 7) & 0x1) ^ (((NZVC&Enu::CMask)/Enu::CMask)*Enu::NMask);
        break;
    case Enu::ApnBp1_func: //A plus ~B plus 1
        carryIn=1;
        //Intentional fallthrough
    case Enu::ApnBpCin_func: //A plus ~B plus Cin
        b=~b;
        //Intentional fallthrough
    case Enu::ApBpCin_func: //A plus B plus Cin
        res=a+b+(int)carryIn;
        NZVC|= Enu::CMask*((int)(res<a||res<b)); //Carry out if result is unsigned less than a or b.
        //V Bit calculation could be wrong
        NZVC|= Enu::VMask*((((a & 0x7f) + (b & 0x7f)) >> 7) & 0x1) ^ (((NZVC&Enu::CMask)/Enu::CMask)*Enu::NMask);
        break;
    case Enu::AandB_func: //A*B
        res=a&b;
        break;
    case Enu::nAandB_func: //~(A*B)
        res=~(a&b);
        break;
    case Enu::AorB_func: //A+B
        res=a|b;
        break;
    case Enu::nAorB_func: //~(A+B)
        res=~(a|b);
        break;
    case Enu::AxorB_func: //A xor B
        res=a^b;
        break;
    case Enu::nA_func: //~A
        res=~a;
        break;
    case Enu::ASLA_func: //ASL A
        res=a<<1;
        //Probably wrong VC bits
        NZVC|=Enu::CMask*((a & 0x80) >> 7);
        NZVC|=Enu::VMask*(((a & 0x40) >> 6) ^ (NZVC&Enu::CMask));
        break;
    case Enu::ROLA_func: //ROL A
        res=a<<1 | ((int) carryIn);
        //Probably wrong VC bits
        NZVC|=Enu::CMask*((a & 0x80) >> 7);
        NZVC|=Enu::VMask*(bool)(((a & 0x40) >> 6) ^ (NZVC&Enu::CMask));
        break;
    case Enu::ASRA_func: //ASR A
        carryIn=true; //RORA and ASRA only differ by whether or not carryIn is guaranteed to be high
        //Intentional fallthrough
    case Enu::RORA_func: //ROR a
        res = (a>>1)|((int)carryIn<<7);
        NZVC|=Enu::CMask*(a&1);
        break;
    case Enu::NZVCA_func: //Move A to NZVC
        res=0;
        NZVC|=Enu::NMask&a;
        NZVC|=Enu::ZMask&a;
        NZVC|=Enu::VMask&a;
        NZVC|=Enu::CMask&a;
        return true; //Must return early to avoid NZ calculation
    default: //If the default has been hit, then an invalid function was selected
        return false;
    }
    //Get boolean value for N, then shift to correct place
    NZVC|=(Enu::NMask*(res>127));
    //Get boolean value for Z, then shift to correct place
    NZVC|=(Enu::ZMask*(res==0));
    return true;

}

void CPUDataSection::setMemoryRegister(Enu::EMemoryRegisters reg, quint8 value)
{
    //Cache old memory value, so it be emitted with signal
    quint8 old = memoryRegisters[reg];
    if(old==value) return; //Don't continue if the new value is the old value
    qDebug()<<"Memory Register"<<reg<<" : "<<value;
    memoryRegisters[reg]=value;
    emit memoryRegisterChanged(reg,old,value);
}

CPUDataSection* CPUDataSection::getInstance()
{
    //If no instance of the CPU data section yet exists, create it
    if(CPUDataSection::_instance==nullptr)
    {
        CPUDataSection::_instance = new CPUDataSection();
    }
    //Return the signle instance of the data section
    return CPUDataSection::_instance;
}

CPUDataSection::~CPUDataSection()
{
    //This code should not be called during the normal lifetime of Pep9CPU
}

quint8 CPUDataSection::getRegisterBankByte(quint8 registerNumber) const
{
    if(registerNumber>Enu::maxRegisterNumber) return 0;
    else return registerBank[registerNumber];

}

quint16 CPUDataSection::getRegisterBankWord(quint8 registerNumber) const
{
    quint16 returnValue;
    if(registerNumber+1>Enu::maxRegisterNumber) returnValue=0;
    else
    {
        returnValue = ((quint16)registerBank[registerNumber])<<8;
        returnValue+=registerBank[registerNumber+1];
    }
    return returnValue;

}

bool CPUDataSection::valueOnABus(quint8 &result) const
{
    if(controlSignals[Enu::A]==Enu::signalDisabled) return false;
    result=getRegisterBankByte(controlSignals[Enu::A]);
    return true;
}

bool CPUDataSection::valueOnBBus(quint8 &result) const
{
    if(controlSignals[Enu::B]==Enu::signalDisabled) return false;
    result=getRegisterBankByte(controlSignals[Enu::B]);
    return true;
}

bool CPUDataSection::valueOnCBus(quint8 &result) const
{
    if(controlSignals[Enu::CMux]==0)
    {
        //If CMux is 0, then the NZVC bits (minus S) are directly routed to result
        result = (NZVCSbits&~(Enu::SMask));
        return true;
    }
    else if(controlSignals[Enu::CMux]==1)
    {
        quint8 temp; //Discard NZVC bits for this calculation, they are unecessary for calculating C's output
        return calculatALUOutput(result,temp);
    }
    else return false;



}

Enu::MainBusState CPUDataSection::getMainBusState() const
{
    return mainBusState;
}

quint8 CPUDataSection::getMemoryByte(quint16 address) const
{
    return memory[address];
}

bool CPUDataSection::getStatusBit(Enu::EStatusBit statusBit) const
{
    switch(statusBit)
    {
    //Mask out bit of interest, then convert to bool
    case Enu::STATUS_N:
        return(NZVCSbits&Enu::NMask);
    case Enu::STATUS_Z:
        return(NZVCSbits&Enu::ZMask);
    case Enu::STATUS_V:
        return(NZVCSbits&Enu::VMask);
    case Enu::STATUS_C:
        return(NZVCSbits&Enu::CMask);
    case Enu::STATUS_S:
        return(NZVCSbits&Enu::SMask);
    }
}

MicroCode* CPUDataSection::getMicrocodeFromSignals() const
{
#pragma message "todo"
    return nullptr;
}

void CPUDataSection::setStatusBitPre(Enu::EStatusBit statusBit, bool val)
{
    qDebug()<<"Status bit "<<statusBit<<":"<<(quint8)val;
    switch(statusBit)
    {
    //Mask out the original bit, and then or it with the properly shifted bit
    case Enu::STATUS_N:
        NZVCSbits=(NZVCSbits&~Enu::NMask)|(val?1:0)*Enu::NMask;
        break;
    case Enu::STATUS_Z:
        NZVCSbits=(NZVCSbits&~Enu::ZMask)|(val?1:0)*Enu::ZMask;
        break;
    case Enu::STATUS_V:
        NZVCSbits=(NZVCSbits&~Enu::VMask)|(val?1:0)*Enu::VMask;
        break;
    case Enu::STATUS_C:
        NZVCSbits=(NZVCSbits&~Enu::CMask)|(val?1:0)*Enu::CMask;
        break;
    case Enu::STATUS_S:
        NZVCSbits=(NZVCSbits&~Enu::SMask)|(val?1:0)*Enu::SMask;
        break;
    }
}

void CPUDataSection::setMemoryBytePre(quint16 address, quint8 val)
{
    qDebug()<<"Memory byte "<<address<<":"<<val;
    memory[address]=val;
}

void CPUDataSection::setMemoryWordPre(quint16 address, quint16 val)
{
    qDebug()<<"Memory word written "<<QString::number(address,16)<<":"<<val;
    memory[address]=val/256;
    memory[address+1]=val%256;
}

void CPUDataSection::setRegisterBytePre(quint8 reg, quint8 val)
{
    if(reg>21) return; //Don't allow static registers to be written to
    qDebug()<<"Register set "<<reg<<":"<<val;
    registerBank[reg]=val;
}

void CPUDataSection::setRegisterWordPre(quint8 reg, quint16 val)
{
   if(reg>21) return; //Don't allow static registers to be written to
   qDebug()<<reg<<"||"<<val;
   registerBank[reg]=val/256;
   registerBank[reg+1]=val%256;
}

void CPUDataSection::setMemoryRegisterPre(Enu::EMemoryRegisters reg, quint8 val)
{
    memoryRegisters[reg]=val;
}

bool CPUDataSection::setSignalsFromMicrocode(const MicroCode *line)
{
    quint32 ctrlChanged=0;
    quint16 clockChanged=0;
    quint8 sig;
    for(int it=0;it<controlSignals.length();it++)
    {
        sig=line->getControlSignal((Enu::EControlSignals)it);
        if(controlSignals[it]!=sig)
        {
            ctrlChanged=(ctrlChanged<<1)+1;
            controlSignals[it]=sig;
        }
    }
    bool val;
    for(int it=0;it<clockSignals.length();it++)
    {
        val=line->getClockSignal((Enu::EClockSignals)it);
        if(clockSignals[it]!=val)
        {
            clockChanged=(clockChanged<<1)+1;
            clockSignals[it]=val;
        }
    }
    if(ctrlChanged||clockChanged) emit controlClockChanged();
    return ctrlChanged|clockChanged;
}

void CPUDataSection::setRegisterByte(quint8 reg, quint8 value)
{
    //Cache old register value
    quint8 old = registerBank[reg];
    if(old==value) return; //Don't continue if the new value is the old value
    setRegisterBytePre(reg,value);
    emit registerChanged(reg,old,value);
}

void CPUDataSection::setMemoryByte(quint16 address, quint8 value)
{
    quint8 old= memory[address];
    if(old == value)return; //Don't continue if the new value is the old value
    setMemoryBytePre(address,value);
    emit memoryChanged(address,old,value);
}

void CPUDataSection::setMemoryWord(quint16 address, quint16 value)
{
    address&=0xFFFE; //Memory access ignores the lowest order bit
    quint8 hi=memory[address],lo=memory[address+1]; //Cache old memory values
    if((((quint16)hi<<8)|lo)==value)return; //Don't continue if the new value is the old value
    setMemoryWordPre(address,value);
    emit memoryChanged(address,hi,value/256); //Signal that two bytes of memory changed
    emit memoryChanged(address+1,lo,value%256);
}

void CPUDataSection::setStatusBit(Enu::EStatusBit statusBit, bool val)
{
    quint8 old = NZVCSbits;
    setStatusBitPre(statusBit,val);
    //Check if old is equal to new after attempting to set it, as there isn't a simple test for bit math
    if(old==val)return; //Prevent signal from being emitted if no value changed
    emit statusBitChanged(statusBit,val);
}

void CPUDataSection::handleMainBusState() noexcept
{
#pragma message "I don't know how the main bus returns to none after ready"
    bool marChanged= false;
    quint8 a,b;
    if(clockSignals[Enu::MARCk]&&valueOnABus(a)&&valueOnBBus(b))
    {
        marChanged=!(a==memoryRegisters[Enu::MEM_MARA]&&b==memoryRegisters[Enu::MEM_MARB]);
    }
    switch(mainBusState)
    {
    case Enu::None:
        //One cannot change MAR contents and initiate a R/W on same cycle
        if(!marChanged)
        {
            if(controlSignals[Enu::MemRead]==1) mainBusState = Enu::MemReadFirstWait;
            else if(controlSignals[Enu::MemWrite]==1) mainBusState = Enu::MemWriteFirstWait;
        }
        break;
    case Enu::MemReadFirstWait:
        if(!marChanged&&controlSignals[Enu::MemRead]==1) mainBusState = Enu::MemReadSecondWait;
        else if(marChanged&&controlSignals[Enu::MemRead]==1); //Initiating a new read brings us back to first wait
        else if(controlSignals[Enu::MemWrite]==1) mainBusState = Enu::MemWriteFirstWait; //Switch from read to write.
        else mainBusState = Enu::None; //If neither are check, bus goes back to doing nothing
        break;
    case Enu::MemReadSecondWait:
        if(!marChanged&&controlSignals[Enu::MemRead]==1) mainBusState = Enu::MemReadReady;
        else if(marChanged&&controlSignals[Enu::MemRead]==1)mainBusState = Enu::MemReadFirstWait;
        else if(controlSignals[Enu::MemWrite]==1) mainBusState = Enu::MemWriteFirstWait;
        else mainBusState = Enu::None; //If neither are check, bus goes back to doing nothing
        break;
    case Enu::MemReadReady:
        if(!marChanged&&controlSignals[Enu::MemRead]==1); //Do nothing, we are already ready
        else if(marChanged&&controlSignals[Enu::MemRead]==1)mainBusState = Enu::MemReadFirstWait;
        else if(controlSignals[Enu::MemWrite]==1) mainBusState = Enu::MemWriteFirstWait;
        else mainBusState = Enu::None; //If neither are check, bus goes back to doing nothing
        break;
    case Enu::MemWriteFirstWait:
        if(!marChanged&&controlSignals[Enu::MemWrite]==1) mainBusState = Enu::MemWriteSecondWait;
        else if(marChanged&&controlSignals[Enu::MemWrite]==1); //Initiating a new write brings us back to first wait
        else if(controlSignals[Enu::MemRead]==1) mainBusState = Enu::MemReadFirstWait; //Switch from write to read.
        else mainBusState = Enu::None; //If neither are check, bus goes back to doing nothing
        break;
    case Enu::MemWriteSecondWait:
        if(!marChanged&&controlSignals[Enu::MemWrite]==1) mainBusState = Enu::MemWriteReady;
        else if(marChanged&&controlSignals[Enu::MemWrite]==1)mainBusState = Enu::MemWriteFirstWait; //Initiating a new write brings us back to first wait
        else if(controlSignals[Enu::MemRead]==1) mainBusState = Enu::MemReadFirstWait; //Switch from write to read.
        else mainBusState = Enu::None; //If neither are check, bus goes back to doing nothing
        break;
    case Enu::MemWriteReady:
        if(!marChanged&&controlSignals[Enu::MemWrite]==1); //Do nothing, MemWrite is already ready
        else if(marChanged&&controlSignals[Enu::MemWrite]==1)mainBusState = Enu::MemWriteFirstWait; //Initiating a new write brings us back to first wait
        else if(controlSignals[Enu::MemRead]==1) mainBusState = Enu::MemReadFirstWait; //Switch from write to read.
        else mainBusState = Enu::None; //If neither are check, bus goes back to doing nothing
        break;
    default:
        mainBusState=Enu::None;
        break;

    }
    qDebug()<<mainBusState;
}

void CPUDataSection::stepOneByte() noexcept
{
    handleMainBusState();
    if(hadErrorOnStep()) return;
    Enu::EALUFunc aluFunc = (Enu::EALUFunc) controlSignals[Enu::ALU];
    quint8 a,b,c,alu,NZVC;
    bool hasA=valueOnABus(a),hasB=valueOnBBus(b),hasC=valueOnCBus(c),aluOutput=calculatALUOutput(alu,NZVC);
    //Handle write to memory
    if(mainBusState == Enu::MemWriteReady)
    {
        quint16 address = (memoryRegisters[Enu::MEM_MARA]<<8)+memoryRegisters[Enu::MEM_MARB];
        setMemoryByte(address,memoryRegisters[Enu::MEM_MDR]);
    }

    //MARCk
    if(clockSignals[Enu::MARCk]&&hasA&&hasB)
    {
        setMemoryRegister(Enu::MEM_MARA,a);
        setMemoryRegister(Enu::MEM_MARB,b);
    }
    else if(clockSignals[Enu::MARCk]) //Handle error where no data is present
    {
        hadDataError=true;
        errorMessage="No values on A & B during MARCk";

    }

    //LoadCk
    if(clockSignals[Enu::LoadCk])
    {
        if(controlSignals[Enu::C]==Enu::signalDisabled)
        {
            hadDataError=true;
            errorMessage = "No destination register specified for LoadCk.";
        }
        else setRegisterByte(controlSignals[Enu::C],c);
    }
    //MDRCk
    if(clockSignals[Enu::MDRCk])
    {
        switch(controlSignals[Enu::MDRMux])
        {
        case 0: //Pick memory
        {
            quint16 address = (memoryRegisters[Enu::MEM_MARA]<<8)+memoryRegisters[Enu::MEM_MARB];
            if(mainBusState!=Enu::MemReadReady){
                hadDataError=true;
                errorMessage = "No value from data bus to write to MDR";
            }
            else setMemoryRegister(Enu::MEM_MDR,getMemoryByte(address));
            break;
        }
        case 1: //Pick C Bus;
        {
            if(!hasC)
            {
                hadDataError=true;
                errorMessage = "No value on C bus to write to MDR";
            }
            else setMemoryRegister(Enu::MEM_MDR,c);
            break;
        }
        default:
            hadDataError=true;
            errorMessage = "No value to clock into MDR";
            break;
        }

    }
    //NCk
    if(clockSignals[Enu::NCk])
    {
        //And against a instead of ALU output, since ALU output is technically 0
        if(aluFunc!=Enu::UNDEFINED_func) setStatusBit(Enu::STATUS_N,Enu::NMask & NZVC);
    }
    //ZCk
    if(clockSignals[Enu::ZCk])
    {
        if(aluFunc!=Enu::UNDEFINED_func) setStatusBit(Enu::STATUS_Z,Enu::ZMask & NZVC);
    }
    //VCk
    if(clockSignals[Enu::VCk])
    {
        if(aluFunc!=Enu::UNDEFINED_func) setStatusBit(Enu::STATUS_V,Enu::VMask & NZVC);
    }
    //CCk
    if(clockSignals[Enu::CCk])
    {
        if(aluFunc!=Enu::UNDEFINED_func) setStatusBit(Enu::STATUS_C,Enu::CMask & NZVC);
    }
    //SCk
    if(clockSignals[Enu::SCk])
    {
        if(aluFunc!=Enu::UNDEFINED_func) setStatusBit(Enu::STATUS_S,Enu::CMask & NZVC);
    }

}

void CPUDataSection::stepTwoByte() noexcept
{
    handleMainBusState();
    if(hadErrorOnStep()) return;
    Enu::EALUFunc aluFunc = (Enu::EALUFunc) controlSignals[Enu::ALU];
    quint8 a,b,c,alu,NZVC;
    bool hasA=valueOnABus(a),hasB=valueOnBBus(b),hasC=valueOnCBus(c),aluOutput=calculatALUOutput(alu,NZVC);
    //Handle write to memory
    if(mainBusState == Enu::MemWriteReady)
    {
        quint16 address = (memoryRegisters[Enu::MEM_MARA]<<8)+memoryRegisters[Enu::MEM_MARB];
        address&=0xFFFE; //Memory access ignores lowest order bit
        setMemoryWord(address,memoryRegisters[Enu::MEM_MDRE]*256+memoryRegisters[Enu::MEM_MDRO]);
    }
    //MARCk
    if(clockSignals[Enu::MARCk])
    {
        if(controlSignals[Enu::MARMux]==0)
        {
            setMemoryRegister(Enu::MEM_MARA,memoryRegisters[Enu::MEM_MDRE]);
            setMemoryRegister(Enu::MEM_MARB,memoryRegisters[Enu::MEM_MDRO]);
        }
        else if(controlSignals[Enu::MARMux]==1&&hasA&&hasB)
        {
            setMemoryRegister(Enu::MEM_MARA,a);
            setMemoryRegister(Enu::MEM_MARB,b);
        }
        else
        {
            hadDataError=true;
            errorMessage = "MARMux has no output but MARCk";
            return;
        }
    }

    //LoadCk
    if(clockSignals[Enu::LoadCk])
    {
        if(controlSignals[Enu::C]==Enu::signalDisabled)
        {
            hadDataError=true;
            errorMessage = "No destination register specified for LoadCk.";
        }
        else setRegisterByte(controlSignals[Enu::C],c);
    }

    //MDRECk
    if(clockSignals[Enu::MDRECk])
    {
        switch(controlSignals[Enu::MDREMux])
        {
        case 0: //Pick memory
        {
            quint16 address = (memoryRegisters[Enu::MEM_MARA]<<8)+memoryRegisters[Enu::MEM_MARB];
            address&=0xFFFE; //Memory access ignores lowest order bit
            if(mainBusState!=Enu::MemReadReady){
                hadDataError=true;
                errorMessage = "No value from data bus to write to MDRE";
                return;
            }
            else setMemoryRegister(Enu::MEM_MDRE,getMemoryByte(address));
            break;
        }
        case 1: //Pick C Bus;
        {
            if(!hasC)
            {
                hadDataError=true;
                errorMessage = "No value on C bus to write to MDRE";
                return;
            }
            else setMemoryRegister(Enu::MEM_MDRE,c);
            break;
        }
        default:
            hadDataError=true;
            errorMessage = "No value to clock into MDRE";
            break;
        }

    }
    //MDRECk
    if(clockSignals[Enu::MDROCk])
    {
        switch(controlSignals[Enu::MDROMux])
        {
        case 0: //Pick memory
        {
            quint16 address = (memoryRegisters[Enu::MEM_MARA]<<8)+memoryRegisters[Enu::MEM_MARB];
            address&=0xFFFE; //Memory access ignores lowest order bit
            address+=1;
            if(mainBusState!=Enu::MemReadReady){
                hadDataError=true;
                errorMessage = "No value from data bus to write to MDRO";
                return;
            }
            else setMemoryRegister(Enu::MEM_MDRO,getMemoryByte(address));
            break;
        }
        case 1: //Pick C Bus;
        {
            if(!hasC)
            {
                hadDataError=true;
                errorMessage = "No value on C bus to write to MDRO";
                return;
            }
            else setMemoryRegister(Enu::MEM_MDRO,c);
            break;
        }
        default:
            hadDataError=true;
            errorMessage = "No value to clock into MDRO";
            break;
        }

    }

    //NCk
    if(clockSignals[Enu::NCk])
    {
        //And against a instead of ALU output, since ALU output is technically 0
        if(aluFunc!=Enu::UNDEFINED_func) setStatusBit(Enu::STATUS_N,Enu::NMask & NZVC);
    }
    //ZCk
    if(clockSignals[Enu::ZCk])
    {
        if(aluFunc!=Enu::UNDEFINED_func) setStatusBit(Enu::STATUS_Z,Enu::ZMask & NZVC);
    }
    //VCk
    if(clockSignals[Enu::VCk])
    {
        if(aluFunc!=Enu::UNDEFINED_func) setStatusBit(Enu::STATUS_V,Enu::VMask & NZVC);
    }
    //CCk
    if(clockSignals[Enu::CCk])
    {
        if(aluFunc!=Enu::UNDEFINED_func) setStatusBit(Enu::STATUS_C,Enu::CMask & NZVC);
    }
    //SCk
    if(clockSignals[Enu::SCk])
    {
        if(aluFunc!=Enu::UNDEFINED_func) setStatusBit(Enu::STATUS_S,Enu::CMask & NZVC);
    }
}

void CPUDataSection::presetStaticRegisters() noexcept
{
    //Pre-assign static registers according to CPU diagram
    registerBank[22]=0x00;
    registerBank[23]=0x01;
    registerBank[24]=0x02;
    registerBank[25]=0x03;
    registerBank[26]=0x04;
    registerBank[27]=0x08;
    registerBank[28]=0xF0;
    registerBank[29]=0xF6;
    registerBank[30]=0xFE;
    registerBank[31]=0xFF;
}

void CPUDataSection::clearControlSignals() noexcept
{
    //Set all control signals to disabled
    for(int it=0; it<controlSignals.length();it++)
    {
        controlSignals[it]=Enu::signalDisabled;
    }
}

void CPUDataSection::clearClockSignals() noexcept
{
    //Set all clock signals to low
    for(int it=0;it<clockSignals.length();it++)
    {
        clockSignals[it]=false;
    }
}

void CPUDataSection::clearRegisters() noexcept
{
    //Clear all registers in register bank, then restore the static values
    for(int it=0;it<registerBank.length();it++)
    {
        registerBank[it]=0;
    }
    presetStaticRegisters();

     //Clear all values from memory registers
    for(int it=0;it<memoryRegisters.length();it++)
    {
        memoryRegisters[it]=0;
    }
}

void CPUDataSection::clearMemory() noexcept
{
    //Set all memory values to 0
    for(int it=0;it<memory.length();it++)
    {
        memory[it]=0;
    }
}

void CPUDataSection::clearErrors() noexcept
{
    hadDataError=false;
    errorMessage="";
}

void CPUDataSection::onStep() noexcept
{
    //If the error hasn't been handled by now, clear it
    clearErrors();
    if(cpuFeatures == Enu::OneByteDataBus) stepOneByte();
    else if(cpuFeatures == Enu::TwoByteDataBus) stepTwoByte();
}

void CPUDataSection::onClock() noexcept
{
    onStep();
    clearClockSignals();
    clearControlSignals();
}

void CPUDataSection::onClearCPU() noexcept
{
    //Reset evey value associated with the CPU
    mainBusState = Enu::None;
    clearErrors();
    clearRegisters();
    clearClockSignals();
    clearControlSignals();
    clearMemory();
}

void CPUDataSection::onClearMemory() noexcept
{
    //On memory reset, only clear RAM
    clearMemory();
}

void CPUDataSection::onCPUFeaturesChanged(Enu::CPUType newFeatures) throw(Enu::InvalidCPUMode)
{
    //Check that the CPU mode is valid, so that no other code has to check.
    //It would be better to crash now than to have a strange error pop up on step(...) later
    if(!(newFeatures!=Enu::OneByteDataBus)&&!(newFeatures!=Enu::TwoByteDataBus))
    {
        throw Enu::InvalidCPUMode();
    }
    cpuFeatures=newFeatures;
    onClearCPU();
    emit CPUFeaturesChanged(newFeatures);
}