#pragma once

#include "J3D/J3DModel.hxx"

class SDLDrawBufToken {

#if __SIZEOF_POINTER__ == 8
#else
    u8 _pc32_tail[1];
#endif
};

class SDLModelData {
    SDLModelData(J3DModelData *data);

    void entryNode(J3DNode *node, SDLDrawBufToken *token);
    void entrySameMat(J3DMaterial *material, SDLDrawBufToken *token);
    void entrySDLModels();
    void recursiveEntry(J3DNode *node, SDLDrawBufToken *token);

#if __SIZEOF_POINTER__ == 8
#else
    u8 _pc32_tail[1];
#endif
};

class SDLModel : public J3DModel {
public:
    SDLModel(SDLModelData *data, u32 flags, u32 unkCount);
    virtual ~SDLModel();

    void entry();
    void entryModelDataSDL(SDLModelData *data, u32 flags, u32 unkCount);
    void viewCalcSimple();

    SDLModelData *mSDLData;
#if __SIZEOF_POINTER__ == 8
#else
    u32 _A4;
#endif

#if __SIZEOF_POINTER__ == 8
#else
    u32 _A8;
#endif
  // Flags?

#if __SIZEOF_POINTER__ == 8
    u8 _pc64_tail[12];
#else
#endif
};
