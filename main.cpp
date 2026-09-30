#include <common/simd/sse.h>

int main()
{
    embree::vboolf4 a(false);
    embree::vboolf4 b(true);
    return (a == b)[0];
}
