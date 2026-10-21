#pragma once

namespace JDrama {

    template <typename T> class TFlagT {
    public:
        TFlagT(T val) : mVal(val) {}
        TFlagT(const TFlagT &);
        void set(T);

        T mVal;
    };

}  // namespace JDrama