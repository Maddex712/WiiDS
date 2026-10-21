package dev.chasem.smslauncher;

import android.content.Context;
import android.content.SharedPreferences;
import org.json.JSONObject;
import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

final class ControllerProfiles {
    static String identity(android.view.InputDevice device) {
        String descriptor=device.getDescriptor();
        return descriptor==null||descriptor.isEmpty()?device.getName():descriptor;
    }
    static final class Profile {
        String descriptor,name;
        int vendor,product;
        final ControllerMapping mapping = new ControllerMapping();
        Profile(String descriptor,String name,int vendor,int product) {
            this.descriptor=descriptor; this.name=name; this.vendor=vendor; this.product=product;
        }
        JSONObject json() throws org.json.JSONException {
            JSONObject bindings=new JSONObject(),labels=new JSONObject();
            for (String key : mapping.bindings.keySet()) {
                bindings.put(key,mapping.bindings.get(key)); labels.put(key,mapping.labels.get(key));
            }
            return new JSONObject().put("descriptor",descriptor).put("name",name).put("vendor",vendor)
                .put("product",product).put("bindings",bindings).put("labels",labels);
        }
        static Profile parse(String text) throws org.json.JSONException {
            JSONObject value=new JSONObject(text);
            Profile p=new Profile(value.getString("descriptor"),value.getString("name"),value.getInt("vendor"),value.getInt("product"));
            JSONObject bindings=value.getJSONObject("bindings"),labels=value.optJSONObject("labels");
            for (String key : ControllerMapping.TARGETS) if (bindings.has(key)) {
                String expression=bindings.getString(key);
                if (ControllerMapping.expression(expression))
                    p.mapping.set(key,expression,labels==null ? "Assigned" : labels.optString(key,"Assigned"));
            }
            return p;
        }
    }
    private static SharedPreferences prefs(Context context) { return context.getSharedPreferences("controller-profiles",Context.MODE_PRIVATE); }
    static List<Profile> saved(Context context) {
        List<Profile> result=new ArrayList<>();
        for (Object text : prefs(context).getAll().values()) if (text instanceof String)
            try { result.add(Profile.parse((String)text)); } catch (org.json.JSONException ignored) {}
        return result;
    }
    static boolean backAssigned(Context context,android.view.InputDevice device) {
        if(device==null||device.isVirtual())return false;
        String text=prefs(context).getString(identity(device),null);if(text==null)return false;
        try{return Profile.parse(text).mapping.bindings.containsValue("b4");}catch(org.json.JSONException e){return false;}
    }
    static void save(Context context,Profile profile) throws java.io.IOException {
        try {
            SharedPreferences.Editor editor=prefs(context).edit();
            if (profile.mapping.bindings.isEmpty()) editor.remove(profile.descriptor);
            else editor.putString(profile.descriptor,profile.json().toString());
            if (!editor.commit()) throw new java.io.IOException("Could not save controller settings.");
        } catch (org.json.JSONException e) { throw new java.io.IOException("Could not save controller settings.",e); }
    }
    static void writeGameFile(Context context) throws java.io.IOException {
        StringBuilder out=new StringBuilder();
        for (Profile p : saved(context)) if (!p.mapping.bindings.isEmpty())
            out.append(String.format(Locale.ROOT,"%04x:%04x:%04x\t",p.vendor,p.product,ControllerMapping.descriptorCrc(p.descriptor)))
                .append(p.mapping.entries()).append('\n');
        File file=new File(ThorStorage.root(context),"controller-mappings.txt"),temp=new File(ThorStorage.root(context),"controller-mappings.tmp");
        Files.write(temp.toPath(),out.toString().getBytes(StandardCharsets.UTF_8));
        Files.move(temp.toPath(),file.toPath(),StandardCopyOption.REPLACE_EXISTING,StandardCopyOption.ATOMIC_MOVE);
    }
}
