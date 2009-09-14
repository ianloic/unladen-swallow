#include "Util/PyTypeBuilder.h"

#include "Python/global_llvm_data.h"

#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/Target/TargetData.h"

unsigned int
_PyTypeBuilder_GetFieldIndexFromOffset(
    const llvm::StructType *type, size_t offset)
{
    static const llvm::TargetData target_data(
        *PyGlobalLlvmData::Get()->getExecutionEngine()->getTargetData());
    const llvm::StructLayout *layout = target_data.getStructLayout(type);
    unsigned int index = layout->getElementContainingOffset(offset);
    assert(layout->getElementOffset(index) == offset &&
           "offset must be at start of element");
    return index;
}
