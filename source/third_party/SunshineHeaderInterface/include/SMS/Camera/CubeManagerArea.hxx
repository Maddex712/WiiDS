#include <Dolphin/MTX.h>
#include <Dolphin/types.h>

#include <JSystem/JDrama/JDRGraphics.hxx>
#include <JSystem/JDrama/JDRViewObj.hxx>
#include <JSystem/JSupport/JSUMemoryStream.hxx>

#include <SMS/Camera/CubeManagerBase.hxx>

class TCubeManagerArea : public TCubeManagerBase {
public:
    ~TCubeManagerArea() override;

    bool isInAreaCube(const Vec &) const;
};