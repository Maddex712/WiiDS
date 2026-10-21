#pragma once

#include <JSystem/JDrama/JDRNameRef.hxx>
#include <JSystem/JDrama/JDRNameRefGen.hxx>

#include <JSystem/JGadget/Vector.hxx>

template <typename DerivedT, typename BaseT = JDrama::TNameRef>
class TNameRefAryT : public JDrama::TNameRef {
public:
    TNameRefAryT(const char *);
    ~TNameRefAryT() override;

    JGadget::TVector<DerivedT> mChildren;
};

template <typename DerivedT, typename BaseT = JDrama::TNameRef>
class TNameRefPtrAryT : public JDrama::TNameRef {
public:
    TNameRefPtrAryT(const char *);
    ~TNameRefPtrAryT() override;

    JGadget::TVector_pointer_void mChildren;
};

class TMarNameRefGen : public JDrama::TNameRefGen {
public:
    virtual JDrama::TNameRef *getNameRef(const char *) const override;

    JDrama::TNameRef *getNameRef_BossEnemy(const char *) const;
    JDrama::TNameRef *getNameRef_Enemy(const char *) const;
    JDrama::TNameRef *getNameRef_Map(const char *) const;
    JDrama::TNameRef *getNameRef_MapObj(const char *) const;
    JDrama::TNameRef *getNameRef_NPC(const char *) const;
};

class TScenarioArchiveName : public JDrama::TNameRef {
public:
    TScenarioArchiveName(const char *name, const char *archiveName)
        : TNameRef(name), mArchiveName(archiveName) {}
    TScenarioArchiveName(const TScenarioArchiveName &);
    ~TScenarioArchiveName() override;
#if __SIZEOF_POINTER__ == 8
    u8 _pc64_0[6];
    const char *mArchiveName;
#else
    const char *mArchiveName;
#endif

};