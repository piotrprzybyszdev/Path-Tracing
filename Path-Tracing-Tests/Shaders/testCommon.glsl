#define BUFFER_POINTER(Name, Type) layout(buffer_reference, buffer_reference_align=8) buffer Name { Type[] v; }

#define TEST_BUFFER(Name) \
    BUFFER_POINTER(Name##InputBuffer, Name##Input); \
    BUFFER_POINTER(Name##OutputBuffer, Name##Output);

#define TEST_READ(Name, index) Name##InputBuffer(pc_InputBuffer).v[index]
#define TEST_WRITE(Name, index, testOutput) Name##OutputBuffer(pc_OutputBuffer).v[index] = testOutput

BUFFER_POINTER(VoidBuffer, bool);
