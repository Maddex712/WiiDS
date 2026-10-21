package dev.chasem.smslauncher;

import java.util.LinkedHashMap;
import java.util.Map;

/** Android input to SDL mapping rules, independent of Activity/storage APIs. */
final class ControllerMapping {
    static final String[] TARGETS = {"a","b","x","y","rightshoulder","lefttrigger","righttrigger","start",
        "leftx","lefty","rightx","righty","dpup","dpdown","dpleft","dpright"};
    final Map<String,String> bindings = new LinkedHashMap<>();
    final Map<String,String> labels = new LinkedHashMap<>();
    static boolean target(String name) {
        for (String value : TARGETS) if (value.equals(name)) return true;
        return false;
    }
    static boolean expression(String value) {
        return value != null && value.matches("none|b(?:[0-9]|[12][0-9]|3[0-5])|[+-]?a(?:[0-9]|[1-5][0-9]|6[0-3])~?");
    }
    void set(String target,String expression,String label) {
        if (!target(target) || !expression(expression)) throw new IllegalArgumentException("Invalid control assignment");
        bindings.put(target,expression); labels.put(target,label);
    }
    String entries() {
        StringBuilder out = new StringBuilder();
        for (String target : TARGETS) if (bindings.containsKey(target))
            out.append(target).append(':').append(bindings.get(target)).append(',');
        return out.toString();
    }
    static int descriptorCrc(String descriptor) {
        int crc = 0;
        // SDL obtains this Java string through JNI GetStringUTFChars, which
        // uses modified UTF-8 (including UTF-16 surrogate pairs).
        for (int at=0;at<descriptor.length();at++) {
            int ch=descriptor.charAt(at);
            if(ch>0&&ch<128)crc=crcByte(crc,ch);
            else if(ch<2048){crc=crcByte(crc,0xc0|(ch>>6));crc=crcByte(crc,0x80|(ch&63));}
            else{crc=crcByte(crc,0xe0|(ch>>12));crc=crcByte(crc,0x80|((ch>>6)&63));crc=crcByte(crc,0x80|(ch&63));}
        }
        return crc & 65535;
    }
    private static int crcByte(int crc,int value) {
        crc^=value;
        for(int i=0;i<8;i++)crc=(crc>>>1)^((crc&1)!=0?0xa001:0);
        return crc;
    }
    static int rawButton(int key) {
        if (key >= 188 && key <= 203) return 20 + key - 188;
        switch (key) {
            case 96: case 23:return 0; case 97:return 1; case 99:return 2; case 100:return 3;
            case 4: case 109:return 4; case 110:return 5; case 82: case 108:return 6;
            case 106:return 7; case 107:return 8; case 102:return 9; case 103:return 10;
            case 19:return 11; case 20:return 12; case 21:return 13; case 22:return 14;
            case 104:return 15; case 105:return 16; case 98:return 17; case 101:return 18;
            default:return -1;
        }
    }
    // Matches SDLControllerManager's ordering: RX/RY precede Z/RZ.
    static int axisOrder(int axis) { return axis == 11 ? 13 : axis > 11 && axis < 14 ? axis-1 : axis; }
    static String axisExpression(int index,double rest,double pressed,boolean stick,boolean trigger) {
        if (index < 0 || index > 63 || !Double.isFinite(rest) || !Double.isFinite(pressed)
            || Math.abs(pressed-rest) < 0.5) return null;
        if (stick) return "a"+index+(pressed < rest ? "~" : "");
        if (trigger && rest < -0.6) return "a"+index;
        if (trigger && rest > 0.6) return "a"+index+"~";
        return (pressed < rest ? "-a" : "+a")+index;
    }
}
