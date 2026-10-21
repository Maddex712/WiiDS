#pragma once

#include <JSystem/JDrama/JDRDStage.hxx>

class TCamConnecter : public JDrama::TViewConnecter {
public:
    ~TCamConnecter() override;

    void perform(u32, JDrama::TGraphics *) override;
};