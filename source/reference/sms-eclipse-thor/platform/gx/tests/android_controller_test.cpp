// Exercise the Android raw-button ABI with SDL's real virtual joystick and
// controller mapping/event conversion. This does not start Android or video.
#include "gx_android_controller_mapping.h"
#include "gx_android_controller_profiles.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>

static bool mappedEvent(Uint32 type, SDL_JoystickID id, int control, int value) {
    bool found = false;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type != type) continue;
        if (type == SDL_CONTROLLERAXISMOTION)
            found |= e.caxis.which == id && e.caxis.axis == control && e.caxis.value == value;
        else found |= e.cbutton.which == id && e.cbutton.button == control;
    }
    return found;
}
int main(int argc, char** argv) {
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    assert(SDL_Init(SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) == 0);
    SDL_VirtualJoystickDesc desc{};
    desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    desc.type = SDL_JOYSTICK_TYPE_UNKNOWN; // Begin as an unmapped raw joystick.
    desc.naxes = 4; desc.nbuttons = 36;
    desc.vendor_id = 0x0810; desc.product_id = 0x0001;
    desc.name = "Android generic numbered-button fixture";
    int index = SDL_JoystickAttachVirtualEx(&desc); assert(index >= 0);
    SDL_Joystick* raw = SDL_JoystickOpen(index); assert(raw);
    SDL_JoystickGUID guid = SDL_JoystickGetGUID(raw);
    char mapping[512];
    auto profile = [&](Uint16 vendor, Uint16 product, int axes, int buttons, int hats,
                       Uint16 mask, Uint16 axisMask, bool mapped) {
        return gx::androidLegacyControllerMapping(guid, vendor, product, axes, buttons, hats,
                                                 mask, axisMask, mapped, mapping, sizeof mapping);
    };
    assert(!profile(0x1234,1,4,36,0,0x7800,0x000f,false));
    assert(!profile(0x0810,2,4,36,0,0x7800,0x000f,false));
    assert(!profile(0x0810,1,6,36,0,0x7800,0x003f,false));
    assert(!profile(0x0810,1,4,16,0,0x7800,0x000f,false));
    assert(!profile(0x0810,1,4,36,1,0x7800,0x000f,false));
    assert(!profile(0x0810,1,4,36,0,0x780f,0x000f,false));
    assert(!profile(0x0810,1,4,36,0,0x7800,0x800f,false));
    assert(!profile(0x0810,1,4,36,0,0x7800,0x000f,true));
    char shortBuffer[16];
    assert(!gx::androidLegacyControllerMapping(guid,0x0810,1,4,36,0,0x7800,0x000f,
                                              false,shortBuffer,sizeof shortBuffer));
    assert(!SDL_IsGameController(index));
    assert(profile(0x0810,1,4,SDL_JoystickNumButtons(raw),0,0x7800,0x000f,false));
    assert(SDL_GameControllerAddMapping(mapping) >= 0);
    assert(SDL_IsGameController(index));
    SDL_GameController* pad = SDL_GameControllerOpen(index); assert(pad);
    const SDL_JoystickID id = SDL_JoystickInstanceID(raw);
    char* legacyText = SDL_GameControllerMapping(pad); assert(legacyText);
    const std::string legacy = legacyText; SDL_free(legacyText); // rightx:a3, righty:a2
    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
    const SDL_GameControllerButton face[] = {SDL_CONTROLLER_BUTTON_Y,SDL_CONTROLLER_BUTTON_B,
        SDL_CONTROLLER_BUTTON_A,SDL_CONTROLLER_BUTTON_X};
    for (int i = 0; i < 12; ++i) {
        const int button = i < 4 ? int(face[i]) : i == 6 ? SDL_CONTROLLER_BUTTON_LEFTSHOULDER :
            i == 7 ? SDL_CONTROLLER_BUTTON_RIGHTSHOULDER : i == 8 ? SDL_CONTROLLER_BUTTON_BACK :
            i == 9 ? SDL_CONTROLLER_BUTTON_START : i == 10 ? SDL_CONTROLLER_BUTTON_LEFTSTICK :
            SDL_CONTROLLER_BUTTON_RIGHTSTICK;
        assert(SDL_JoystickSetVirtualButton(raw,20+i,1) == 0); SDL_JoystickUpdate();
        if (i == 4 || i == 5) {
            const SDL_GameControllerAxis trigger = i == 4 ? SDL_CONTROLLER_AXIS_TRIGGERLEFT : SDL_CONTROLLER_AXIS_TRIGGERRIGHT;
            assert(SDL_GameControllerGetAxis(pad,trigger) == 32767);
            assert(mappedEvent(SDL_CONTROLLERAXISMOTION,id,trigger,32767));
        } else {
            assert(SDL_GameControllerGetButton(pad,SDL_GameControllerButton(button)) == 1);
            assert(mappedEvent(SDL_CONTROLLERBUTTONDOWN,id,button,1));
        }
        assert(SDL_JoystickSetVirtualButton(raw,20+i,0) == 0); SDL_JoystickUpdate();
        if (i == 4 || i == 5) {
            const SDL_GameControllerAxis trigger = i == 4 ? SDL_CONTROLLER_AXIS_TRIGGERLEFT : SDL_CONTROLLER_AXIS_TRIGGERRIGHT;
            assert(SDL_GameControllerGetAxis(pad,trigger) == 0);
            assert(mappedEvent(SDL_CONTROLLERAXISMOTION,id,trigger,0));
        } else {
            assert(SDL_GameControllerGetButton(pad,SDL_GameControllerButton(button)) == 0);
            assert(mappedEvent(SDL_CONTROLLERBUTTONUP,id,button,0));
        }
    }
    for (int i = 0; i < 4; ++i) {
        const SDL_GameControllerButton button = SDL_GameControllerButton(SDL_CONTROLLER_BUTTON_DPAD_UP+i);
        assert(SDL_JoystickSetVirtualButton(raw,11+i,1) == 0); SDL_JoystickUpdate();
        assert(SDL_GameControllerGetButton(pad,button) == 1);
        assert(mappedEvent(SDL_CONTROLLERBUTTONDOWN,id,button,1));
        assert(SDL_JoystickSetVirtualButton(raw,11+i,0) == 0); SDL_JoystickUpdate();
        assert(mappedEvent(SDL_CONTROLLERBUTTONUP,id,button,0));
    }
    const SDL_GameControllerAxis axes[] = {SDL_CONTROLLER_AXIS_LEFTX,SDL_CONTROLLER_AXIS_LEFTY,
        SDL_CONTROLLER_AXIS_RIGHTY,SDL_CONTROLLER_AXIS_RIGHTX};
    for (int i = 0; i < 4; ++i) {
        for (Sint16 value : {Sint16(-32768),Sint16(32767),Sint16(0)}) {
            assert(SDL_JoystickSetVirtualAxis(raw,i,value) == 0); SDL_JoystickUpdate();
            assert(SDL_GameControllerGetAxis(pad,axes[i]) == value);
            assert(mappedEvent(SDL_CONTROLLERAXISMOTION,id,axes[i],value));
        }
    }
    assert(!profile(0x0810,1,4,36,0,0x7800,0x000f,SDL_IsGameController(index)));
    // Settings override the automatic profile, preserve untouched controls,
    // and explicitly clear controls. Exercise the actual SDL remapper while
    // the controller is open, not just the generated string.
    gx::AndroidControllerProfile user;
    char record[512];
    const Uint16 crc = Uint16(guid.data[2] | Uint16(guid.data[3]) << 8);
    assert(SDL_crc16(0,"123456789",9) == 0xbb3d); // Java descriptor CRC contract.
    assert(SDL_crc16(0,"\xc0\x80",2) == 0xa051);
    assert(SDL_crc16(0,"\xed\xa0\xbd\xed\xb8\x80",6) == 0x9d50);
    SDL_snprintf(record,sizeof record,"0810:0001:%04x\ta:b20,y:none,rightx:a2~,righty:none,lefttrigger:+a0,leftx:none,paddle1:b32,-lefty:b33,+rightx:b34,",crc);
    assert(gx::parseControllerProfile(record,user));
    assert(gx::controllerProfileMatches(user,guid,0x0810,1));
    assert(!gx::controllerProfileMatches(user,guid,0x0810,2));
    SDL_JoystickGUID other = guid; other.data[2] ^= 1;
    assert(!gx::controllerProfileMatches(user,other,0x0810,1));
    gx::AndroidControllerProfile bad;
    for (const char* invalid : {"0810:0001:0000\t", "0810:0001:0000\tcrc:abcd,", "0810:0001:0000\ta:b36,",
        "0810:0001:0000\ta:b1,a:b2,", "0810:0001:0000\trightx:a9999,", "0810:0001:0000\ta:b1~,",
        "0810:0001:0000\t+a:b1,", "0810:0001:0000\tpaddle3:b1,", "0810:0001:0000\t+leftx:b1,+leftx:b2,"})
        assert(!gx::parseControllerProfile(invalid,bad));
    assert(gx::controllerHidIgnore({user,user},"0x1234/0x0002") == "0x1234/0x0002,0x0810/0x0001");
    gx::AndroidControllerProfile unidentified=user;unidentified.product=0;
    assert(gx::controllerHidIgnore({unidentified},nullptr).empty());
    assert(argc==2);
    FILE* savedFile=std::fopen(argv[1],"w");assert(savedFile);
    assert(std::fprintf(savedFile,"invalid line\n%s\n",record)>0);assert(std::fclose(savedFile)==0);
    const auto savedProfiles=gx::readControllerProfiles(argv[1]);assert(savedProfiles.size()==1);
    assert(gx::controllerProfileMatches(savedProfiles[0],guid,0x0810,1));
    assert(savedProfiles[0].overrides==user.overrides);assert(std::remove(argv[1])==0);
    char* baseMapping = SDL_GameControllerMapping(pad); std::string userMapping;
    assert(gx::userControllerMapping(guid,user,4,36,baseMapping,userMapping));
    assert(!gx::userControllerMapping(guid,user,2,36,baseMapping,userMapping));
    assert(gx::userControllerMapping(guid,user,4,36,baseMapping,userMapping));
    SDL_free(baseMapping);
    assert(SDL_GameControllerAddMapping(userMapping.c_str()) >= 0);
    assert(SDL_JoystickSetVirtualButton(raw,20,1) == 0); SDL_JoystickUpdate();
    assert(SDL_GameControllerGetButton(pad,SDL_CONTROLLER_BUTTON_A) == 1);
    assert(SDL_GameControllerGetButton(pad,SDL_CONTROLLER_BUTTON_Y) == 0);
    assert(mappedEvent(SDL_CONTROLLERBUTTONDOWN,id,SDL_CONTROLLER_BUTTON_A,1));
    assert(SDL_JoystickSetVirtualButton(raw,20,0) == 0); SDL_JoystickUpdate();
    assert(mappedEvent(SDL_CONTROLLERBUTTONUP,id,SDL_CONTROLLER_BUTTON_A,0));
    assert(SDL_JoystickSetVirtualButton(raw,22,1) == 0); SDL_JoystickUpdate();
    assert(SDL_GameControllerGetButton(pad,SDL_CONTROLLER_BUTTON_A) == 0);
    assert(SDL_JoystickSetVirtualButton(raw,22,0) == 0); SDL_JoystickUpdate();
    assert(SDL_JoystickSetVirtualButton(raw,29,1) == 0); SDL_JoystickUpdate();
    assert(SDL_GameControllerGetButton(pad,SDL_CONTROLLER_BUTTON_START) == 1); // untouched default
    assert(SDL_JoystickSetVirtualButton(raw,29,0) == 0); SDL_JoystickUpdate();
    assert(SDL_JoystickSetVirtualAxis(raw,2,32767) == 0); SDL_JoystickUpdate();
    assert(SDL_GameControllerGetAxis(pad,SDL_CONTROLLER_AXIS_RIGHTX) == -32768);
    assert(SDL_GameControllerGetAxis(pad,SDL_CONTROLLER_AXIS_RIGHTY) == 0);
    assert(SDL_JoystickSetVirtualAxis(raw,2,0) == 0); SDL_JoystickUpdate();
    assert(SDL_JoystickSetVirtualAxis(raw,0,32767) == 0); SDL_JoystickUpdate();
    assert(SDL_GameControllerGetAxis(pad,SDL_CONTROLLER_AXIS_TRIGGERLEFT) == 32767);
    assert(SDL_GameControllerGetAxis(pad,SDL_CONTROLLER_AXIS_LEFTX) == 0);
    assert(SDL_JoystickSetVirtualAxis(raw,0,0) == 0); SDL_JoystickUpdate();
    assert(SDL_GameControllerGetAxis(pad,SDL_CONTROLLER_AXIS_TRIGGERLEFT) == 0);
    // A soft-press carrier and buttons pushing one stick direction, alongside
    // the stick axes they share.
    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
    assert(SDL_JoystickSetVirtualButton(raw,32,1) == 0); SDL_JoystickUpdate();
    assert(SDL_GameControllerGetButton(pad,SDL_CONTROLLER_BUTTON_PADDLE1) == 1);
    assert(mappedEvent(SDL_CONTROLLERBUTTONDOWN,id,SDL_CONTROLLER_BUTTON_PADDLE1,1));
    assert(SDL_JoystickSetVirtualButton(raw,32,0) == 0); SDL_JoystickUpdate();
    assert(SDL_GameControllerGetButton(pad,SDL_CONTROLLER_BUTTON_PADDLE1) == 0);
    assert(SDL_JoystickSetVirtualButton(raw,33,1) == 0); SDL_JoystickUpdate();
    assert(SDL_GameControllerGetAxis(pad,SDL_CONTROLLER_AXIS_LEFTY) == -32768);
    assert(mappedEvent(SDL_CONTROLLERAXISMOTION,id,SDL_CONTROLLER_AXIS_LEFTY,-32768));
    assert(SDL_JoystickSetVirtualButton(raw,33,0) == 0); SDL_JoystickUpdate();
    assert(SDL_GameControllerGetAxis(pad,SDL_CONTROLLER_AXIS_LEFTY) == 0);
    assert(SDL_JoystickSetVirtualAxis(raw,1,32767) == 0); SDL_JoystickUpdate();
    assert(SDL_GameControllerGetAxis(pad,SDL_CONTROLLER_AXIS_LEFTY) == 32767); // untouched stick axis
    assert(SDL_JoystickSetVirtualAxis(raw,1,0) == 0); SDL_JoystickUpdate();
    assert(SDL_JoystickSetVirtualButton(raw,34,1) == 0); SDL_JoystickUpdate();
    assert(SDL_GameControllerGetAxis(pad,SDL_CONTROLLER_AXIS_RIGHTX) == 32767);
    assert(SDL_JoystickSetVirtualButton(raw,34,0) == 0); SDL_JoystickUpdate();
    assert(mappedEvent(SDL_CONTROLLERAXISMOTION,id,SDL_CONTROLLER_AXIS_RIGHTX,0));
    assert(SDL_abs(SDL_GameControllerGetAxis(pad,SDL_CONTROLLER_AXIS_RIGHTX)) <= 1); // inverted a2 at rest
    // A stick direction moved to an axis replaces that direction of the
    // stick's automatic whole-axis binding, which SDL would otherwise match
    // first; directions the player left alone keep the automatic binding.
    auto configure = [&](const char* entries) {
        char line[256]; gx::AndroidControllerProfile directions; std::string text;
        SDL_snprintf(line,sizeof line,"0810:0001:%04x\t%s",crc,entries);
        assert(gx::parseControllerProfile(line,directions));
        assert(gx::userControllerMapping(guid,directions,4,36,legacy.c_str(),text));
        assert(text.find(",:") == std::string::npos && text.find(",,") == std::string::npos);
        for (int axis = 0; axis < 4; ++axis) assert(SDL_JoystickSetVirtualAxis(raw,axis,0) == 0);
        SDL_JoystickUpdate();
        assert(SDL_GameControllerAddMapping(text.c_str()) >= 0);
        return text;
    };
    // The game reads controller events: SDL gives a raw axis value to the
    // first binding whose input range holds it. Returns the output's event
    // value (0 if none), checked against the polled value, and checks that
    // `unmoved` sent nothing.
    auto moved = [&](int axis, Sint16 value, SDL_GameControllerAxis output,
                     SDL_GameControllerAxis unmoved = SDL_CONTROLLER_AXIS_INVALID) {
        SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
        assert(SDL_JoystickSetVirtualAxis(raw,axis,value) == 0); SDL_JoystickUpdate();
        int sent = 0; SDL_Event e;
        while (SDL_PollEvent(&e)) if (e.type == SDL_CONTROLLERAXISMOTION && e.caxis.which == id) {
            if (e.caxis.axis == output) sent = e.caxis.value;
            assert(e.caxis.axis != unmoved || e.caxis.value == 0);
        }
        assert(unmoved != SDL_CONTROLLER_AXIS_INVALID || SDL_GameControllerGetAxis(pad,output) == sent);
        assert(SDL_JoystickSetVirtualAxis(raw,axis,0) == 0); SDL_JoystickUpdate();
        return sent;
    };
    // Both camera directions swapped through the Left and Right rows.
    std::string swapped = configure("-rightx:+a3,+rightx:-a3,");
    assert(swapped.find(",rightx:a3,") == std::string::npos);
    assert(moved(3,32767,SDL_CONTROLLER_AXIS_RIGHTX) == -32768);
    assert(moved(3,-32768,SDL_CONTROLLER_AXIS_RIGHTX) == 32767);
    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
    assert(SDL_JoystickSetVirtualAxis(raw,3,32767) == 0); SDL_JoystickUpdate();
    assert(mappedEvent(SDL_CONTROLLERAXISMOTION,id,SDL_CONTROLLER_AXIS_RIGHTX,-32768));
    assert(SDL_JoystickSetVirtualAxis(raw,3,0) == 0); SDL_JoystickUpdate();
    assert(SDL_GameControllerGetAxis(pad,SDL_CONTROLLER_AXIS_RIGHTX) == 0);
    assert(moved(2,32767,SDL_CONTROLLER_AXIS_RIGHTY) == 32767); // other axes untouched
    assert(moved(0,-32768,SDL_CONTROLLER_AXIS_LEFTX) == -32768);
    // One row: Left is now the stick's right; Right keeps the automatic
    // binding, so the stick's left (now unbound) does nothing.
    std::string one = configure("-rightx:+a3,");
    assert(one.find("+rightx:+a3,") != std::string::npos && one.find(",rightx:a3,") == std::string::npos);
    assert(moved(3,32767,SDL_CONTROLLER_AXIS_RIGHTX) == -32768);
    assert(moved(3,-32768,SDL_CONTROLLER_AXIS_RIGHTX) == 0);
    // An inverted whole-axis row split around a cleared direction.
    configure("rightx:a3~,-rightx:none,");
    assert(moved(3,32767,SDL_CONTROLLER_AXIS_RIGHTX) == 0);
    assert(moved(3,-32768,SDL_CONTROLLER_AXIS_RIGHTX) == 32767);
    // The player's direction wins over another automatic binding of the same
    // raw axis half; that binding keeps the other half.
    configure("+rightx:+a0,");
    // (Polling LEFTX would still read a0 through leftx:a0; the game doesn't poll.)
    assert(moved(0,32767,SDL_CONTROLLER_AXIS_RIGHTX,SDL_CONTROLLER_AXIS_LEFTX) == 32767);
    assert(moved(0,-32768,SDL_CONTROLLER_AXIS_LEFTX) == -32768);
    assert(moved(3,32767,SDL_CONTROLLER_AXIS_RIGHTX) == 0);
    assert(moved(3,-32768,SDL_CONTROLLER_AXIS_RIGHTX) == -32768);
    // A whole inverted row alone is unchanged.
    configure("rightx:a3~,");
    assert(moved(3,32767,SDL_CONTROLLER_AXIS_RIGHTX) == -32768);
    assert(moved(3,-32768,SDL_CONTROLLER_AXIS_RIGHTX) == 32767);
    SDL_GameControllerClose(pad); SDL_JoystickClose(raw);
    assert(SDL_JoystickDetachVirtual(index) == 0);
    // Mapping persists across a disconnect; a newly attached matching device
    // must be recognized normally and gets a distinct instance ID.
    index = SDL_JoystickAttachVirtualEx(&desc); assert(index >= 0 && SDL_IsGameController(index));
    pad = SDL_GameControllerOpen(index); assert(pad);
    assert(SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad)) != id);
    assert(SDL_GameControllerGetButton(pad,SDL_CONTROLLER_BUTTON_A) == 0);
    SDL_GameControllerClose(pad); assert(SDL_JoystickDetachVirtual(index) == 0);
    SDL_Quit();
    std::puts("PASS Android controller mapping: legacy input, saved overrides, clears, untouched defaults, soft-press carriers, stick directions, direction rows over automatic stick axes, device identity, malformed profiles, inversion, triggers, events and reconnect");
}
