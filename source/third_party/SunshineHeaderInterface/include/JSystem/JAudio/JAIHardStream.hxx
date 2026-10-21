#pragma once

#include <Dolphin/DVD.h>
#include <Dolphin/types.h>

namespace JASystem {
    namespace HardStream {
        struct TPlayInfo {
            u32 _00;
            u16 mIntroCount;  // Song to play (index into file array)
            u16 mLoopCount;
        };

        struct TFrame {
            TPlayInfo *mInfo;
            u32 _04;
            u32 _08;
        };

        class TControl {
        public:
            enum State {
                IDLE        = 0,
                PLAY_FIRST  = 1,
                PLAY_SECOND = 2,
                PLAYING     = 3,
                POKE_ADDR   = 4,
                NEXT        = 5,
                STOP        = 6
            };

            TControl();
            ~TControl();

            void calcCurVolume();
            void clearListOne();
            u8 getCurVol();
            s16 getIntroNum();
            u32 getLastAddr();
            s16 getLoopNum();
            void resetFader();
            void startFirst(u16 fileIndex, DVDFileInfo *fileInfos, u32 *infoToSelect);
            void startSecond(u16 fileIndex, DVDFileInfo *fileInfos, u32 *infoToSelect);
            u8 volFloatToU8(f32 volume);

            u32 _00;
            TFrame *mFrame;
            u16 _08;
            u8 _0A;
            u8 mState;
            f32 _0C;
            f32 mVolume;
            u32 mLooping;  // ?
            u32 _18;
            u32 _1C;
            f32 _20;
            f32 _24;
            f32 _28;
            f32 _2C;
            f32 _30;
            f32 _34;
            u32 _38;
            u32 _3C;
            u32 _40;
            u32 _44;
            u32 _48;
            u32 _4C;
        };
        void main();

        void firstBgmCallback(s32, DVDFileInfo *fi);
        void secondBgmCallback(s32, DVDFileInfo *fi);
        void getAddrCallback(s32, DVDCommandBlock *cmd);

        extern u32 playList;
        extern u32 playListMax;
        extern char rootDir[32];
        extern TControl strCtrl;
        extern char *streamFiles;
        extern bool useHardStreaming;

        constexpr size_t streamFilesSize = 36;
    }  // namespace HardStream
}  // namespace JASystem

extern DVDFileInfo hardstream_finfos[3];
extern u32 hardstream_cur_finfo;