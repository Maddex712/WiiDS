package dev.chasem.smslauncher;

import android.app.Activity;
import android.app.AlertDialog;
import android.graphics.Color;
import android.hardware.input.InputManager;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.widget.*;
import java.util.*;

/** User-driven per-device assignments, captured before SDL sees input. */
public class ControllerConfigActivity extends Activity implements InputManager.InputDeviceListener {
    private static final String[] NAMES={"A · Jump","B · Dive","X","Y","Z","L","R · Spray","Start · Pause",
        "Movement · Horizontal","Movement · Vertical","Camera · Horizontal","Camera · Vertical","D-pad · Up","D-pad · Down","D-pad · Left","D-pad · Right"};
    private final Map<String,ControllerProfiles.Profile> drafts=new LinkedHashMap<>();
    private final Set<String> dirty=new HashSet<>();
    private final Map<Integer,Double> latest=new HashMap<>(),rest=new HashMap<>();
    private final Set<Integer> heldKeys=new HashSet<>();
    private final Handler handler=new Handler(Looper.getMainLooper());
    private InputManager input;
    private Spinner selector;
    private LinearLayout assignments;
    private TextView instruction,live;
    private Button save,cancel;
    private List<String> deviceChoices=new ArrayList<>();
    private String selected,pending;
    private boolean active,refreshing,liveScheduled,motionPrimed,seenMotion;
    private float hatX,hatY,restHatX,restHatY;
    private String liveMessage="";
    private int dp(int n) { return Math.round(n*getResources().getDisplayMetrics().density); }
    private TextView text(String value,int size) {
        TextView v=new TextView(this);v.setText(value);v.setTextSize(size);v.setTextColor(Color.WHITE);
        v.setPadding(dp(10),dp(5),dp(10),dp(5));return v;
    }
    private Button button(LinearLayout row,String label,Runnable action) {
        Button b=new Button(this);b.setText(label);b.setAllCaps(false);b.setOnClickListener(v->action.run());row.addView(b);return b;
    }
    private LinearLayout panel(LinearLayout columns,String title,int weight) {
        LinearLayout card=new LinearLayout(this);card.setOrientation(LinearLayout.VERTICAL);card.setBackgroundColor(Color.rgb(18,36,52));
        LinearLayout.LayoutParams size=new LinearLayout.LayoutParams(0,-1,weight);size.setMargins(dp(4),0,dp(4),0);columns.addView(card,size);
        card.addView(text(title+" · scroll",18));
        ScrollView scroll=new ScrollView(this);scroll.setScrollbarFadingEnabled(false);scroll.setScrollBarStyle(View.SCROLLBARS_OUTSIDE_INSET);
        android.graphics.drawable.GradientDrawable thumb=new android.graphics.drawable.GradientDrawable();
        thumb.setColor(Color.rgb(140,170,190));thumb.setSize(dp(4),dp(24));scroll.setVerticalScrollbarThumbDrawable(thumb);
        card.addView(scroll,new LinearLayout.LayoutParams(-1,0,1));
        LinearLayout content=new LinearLayout(this);content.setOrientation(LinearLayout.VERTICAL);scroll.addView(content);return content;
    }
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);input=getSystemService(InputManager.class);
        for (ControllerProfiles.Profile p:ControllerProfiles.saved(this)) drafts.put(p.descriptor,p);
        if(state!=null) {
            selected=state.getString("selected");ArrayList<String> savedDrafts=state.getStringArrayList("drafts");
            if(savedDrafts!=null)for(String value:savedDrafts)try {ControllerProfiles.Profile p=ControllerProfiles.Profile.parse(value);drafts.put(p.descriptor,p);}catch(org.json.JSONException ignored){}
            ArrayList<String> changed=state.getStringArrayList("dirty");if(changed!=null)dirty.addAll(changed);
        }
        LinearLayout root=new LinearLayout(this);root.setOrientation(LinearLayout.VERTICAL);root.setBackgroundColor(Color.rgb(12,26,42));
        LinearLayout top=new LinearLayout(this);top.addView(text("Controller configuration",20),new LinearLayout.LayoutParams(0,-2,1));
        save=button(top,"Save",this::save);button(top,"Done",this::done);root.addView(top);
        root.addView(text("Select a controller, then Assign a control. Release controls before assigning. Save applies your changes on the next Play.",13));
        instruction=text("Unchanged controls keep the automatic layout. Clear disables a control.",14);root.addView(instruction);
        LinearLayout columns=new LinearLayout(this);root.addView(columns,new LinearLayout.LayoutParams(-1,0,1));
        LinearLayout devices=panel(columns,"Controller",1);selector=new Spinner(this);selector.setBackgroundColor(Color.rgb(40,60,80));devices.addView(selector);
        button(devices,"Refresh devices",this::inventory);button(devices,"Restore defaults",()->{
            ControllerProfiles.Profile p=profile();if(p==null)return;
            p.mapping.bindings.clear();p.mapping.labels.clear();dirty.add(selected);cancelCapture();rows();
            instruction.setText("Defaults restored in this draft. Press Save to apply.");
        });
        devices.addView(text("Mappings are saved separately for each controller, including disconnected controllers. Android Back or Done returns to the launcher.",13));
        devices.addView(text("Custom mappings do not support controller rumble. Restore defaults and Save to return to the automatic layout.",13));
        assignments=panel(columns,"GameCube controls",2);
        LinearLayout bottom=new LinearLayout(this);live=text("Connect and select a controller.",12);bottom.addView(live,new LinearLayout.LayoutParams(0,-2,1));
        cancel=button(bottom,"Cancel capture",this::cancelCapture);cancel.setVisibility(View.GONE);root.addView(bottom);
        root.addView(text(BuildInfo.summary(),12));
        root.setOnApplyWindowInsetsListener((view,insets)->{
            android.graphics.Insets safe=android.os.Build.VERSION.SDK_INT>=30?insets.getInsets(android.view.WindowInsets.Type.systemBars()|android.view.WindowInsets.Type.displayCutout()):
                android.graphics.Insets.of(insets.getSystemWindowInsetLeft(),insets.getSystemWindowInsetTop(),insets.getSystemWindowInsetRight(),insets.getSystemWindowInsetBottom());
            view.setPadding(safe.left,safe.top,safe.right,safe.bottom);return insets;
        });setContentView(root);
        if(android.os.Build.VERSION.SDK_INT>=33)getOnBackInvokedDispatcher().registerOnBackInvokedCallback(android.window.OnBackInvokedDispatcher.PRIORITY_DEFAULT,this::done);
        selector.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener(){
            public void onItemSelected(AdapterView<?> parent,View view,int pos,long id){
                if(refreshing||pos>=deviceChoices.size())return;
                String next=deviceChoices.get(pos);
                if(!next.equals(selected)){cancelCapture();latest.clear();heldKeys.clear();seenMotion=false;selected=next;}rows();
            }
            public void onNothingSelected(AdapterView<?> parent){}
        });
    }
    private static boolean controller(InputDevice d) {
        return d!=null&&!d.isVirtual()&&(d.supportsSource(InputDevice.SOURCE_JOYSTICK)||d.supportsSource(InputDevice.SOURCE_GAMEPAD)||
            (d.supportsSource(InputDevice.SOURCE_DPAD)&&d.getKeyboardType()!=InputDevice.KEYBOARD_TYPE_ALPHABETIC));
    }
    private InputDevice connected() {
        if(selected==null)return null;
        for(int id:InputDevice.getDeviceIds()){InputDevice d=InputDevice.getDevice(id);if(d!=null&&selected.equals(ControllerProfiles.identity(d)))return d;}return null;
    }
    private ControllerProfiles.Profile profile(){return selected==null?null:drafts.get(selected);}
    private void inventory() {
        cancelCapture();
        for(int id:InputDevice.getDeviceIds()){
            InputDevice d=InputDevice.getDevice(id);if(!controller(d))continue;
            if(!drafts.containsKey(ControllerProfiles.identity(d)))drafts.put(ControllerProfiles.identity(d),new ControllerProfiles.Profile(ControllerProfiles.identity(d),d.getName(),d.getVendorId(),d.getProductId()));
        }
        deviceChoices=new ArrayList<>(drafts.keySet());List<String> names=new ArrayList<>();
        for(String key:deviceChoices){boolean found=false;for(int id:InputDevice.getDeviceIds()){InputDevice d=InputDevice.getDevice(id);if(d!=null&&key.equals(ControllerProfiles.identity(d)))found=true;}
            names.add(drafts.get(key).name+(found?"":" (disconnected)"));}
        refreshing=true;selector.setAdapter(new ArrayAdapter<>(this,android.R.layout.simple_spinner_dropdown_item,names));
        if(selected==null||!drafts.containsKey(selected))selected=deviceChoices.isEmpty()?null:deviceChoices.get(0);
        if(selected!=null)selector.setSelection(deviceChoices.indexOf(selected));refreshing=false;rows();
    }
    private void rows(){
        assignments.removeAllViews();ControllerProfiles.Profile p=profile();save.setEnabled(p!=null&&pending==null);
        if(p==null){assignments.addView(text("No controller found. Connect one and press Refresh devices.",16));return;}
        for(int i=0;i<ControllerMapping.TARGETS.length;i++){
            final String target=ControllerMapping.TARGETS[i];LinearLayout row=new LinearLayout(this);
            String label=p.mapping.labels.get(target);if(label==null)label="Automatic";
            TextView title=text(NAMES[i]+"\n"+label,14);row.addView(title,new LinearLayout.LayoutParams(0,-2,1));
            Button assign=button(row,"Assign",()->arm(target));assign.setEnabled(connected()!=null);
            button(row,"Clear",()->{p.mapping.set(target,"none","Unassigned");dirty.add(p.descriptor);cancelCapture();rows();});assignments.addView(row);
        }
    }
    private void arm(String target){
        if(connected()==null)return;pending=target;rest.clear();rest.putAll(latest);restHatX=hatX;restHatY=hatY;motionPrimed=seenMotion;cancel.setVisibility(View.VISIBLE);save.setEnabled(false);
        String move=target.equals("leftx")?"Move the LEFT stick RIGHT":target.equals("lefty")?"Move the LEFT stick DOWN":
            target.equals("rightx")?"Move the RIGHT stick RIGHT":target.equals("righty")?"Move the RIGHT stick DOWN":
            target.equals("dpup")?"Press D-pad UP":target.equals("dpdown")?"Press D-pad DOWN":target.equals("dpleft")?"Press D-pad LEFT":target.equals("dpright")?"Press D-pad RIGHT":"Press the button or trigger you want to use";
        instruction.setText(move+". Only input from the selected controller is captured.");
    }
    private void cancelCapture(){if(pending!=null&&instruction!=null)instruction.setText("Capture cancelled. Tap Assign to try again.");pending=null;rest.clear();if(cancel!=null)cancel.setVisibility(View.GONE);if(save!=null)save.setEnabled(profile()!=null);}
    private void captured(String expression,String label){
        if(pending==null||profile()==null)return;
        profile().mapping.set(pending,expression,label);dirty.add(selected);cancelCapture();rows();instruction.setText("Assigned "+label+". Press Save when finished.");
    }
    private void showLive(String value){liveMessage=value;if(liveScheduled)return;liveScheduled=true;handler.postDelayed(()->{liveScheduled=false;live.setText(liveMessage);},50);}
    private void save(){
        if(profile()==null||pending!=null)return;
        try{ControllerProfiles.save(this,profile());dirty.remove(selected);instruction.setText("Saved. These controls apply on the next Play.");}
        catch(java.io.IOException e){instruction.setText(e.getMessage());}
    }
    private void done(){if(dirty.isEmpty())finish();else new AlertDialog.Builder(this).setMessage("Discard unsaved controller changes?").setNegativeButton("Keep editing",null).setPositiveButton("Discard",(d,w)->finish()).show();}
    @Override public void onBackPressed(){done();}
    @Override protected void onResume(){super.onResume();active=true;if(input!=null)input.registerInputDeviceListener(this,handler);inventory();}
    @Override protected void onPause(){active=false;cancelCapture();heldKeys.clear();latest.clear();seenMotion=false;if(input!=null)input.unregisterInputDeviceListener(this);handler.removeCallbacksAndMessages(null);liveScheduled=false;super.onPause();}
    @Override public void onWindowFocusChanged(boolean focus){super.onWindowFocusChanged(focus);if(!focus){cancelCapture();heldKeys.clear();}}
    @Override protected void onSaveInstanceState(Bundle state){
        super.onSaveInstanceState(state);state.putString("selected",selected);state.putStringArrayList("dirty",new ArrayList<>(dirty));ArrayList<String> values=new ArrayList<>();
        for(ControllerProfiles.Profile p:drafts.values())try{values.add(p.json().toString());}catch(org.json.JSONException ignored){}state.putStringArrayList("drafts",values);
    }
    @Override public void onInputDeviceAdded(int id){inventory();}
    @Override public void onInputDeviceChanged(int id){latest.clear();heldKeys.clear();seenMotion=false;inventory();}
    @Override public void onInputDeviceRemoved(int id){latest.clear();heldKeys.clear();seenMotion=false;inventory();}
    @Override public boolean dispatchKeyEvent(KeyEvent event){
        if(!active||!controller(event.getDevice()))return super.dispatchKeyEvent(event);
        if(selected!=null&&selected.equals(ControllerProfiles.identity(event.getDevice()))){
            boolean fresh=event.getAction()==KeyEvent.ACTION_DOWN&&event.getRepeatCount()==0&&heldKeys.add(event.getKeyCode());
            if(event.getAction()==KeyEvent.ACTION_UP)heldKeys.remove(event.getKeyCode());
            showLive(KeyEvent.keyCodeToString(event.getKeyCode())+(event.getAction()==KeyEvent.ACTION_UP?" · released":" · pressed"));
            if(pending!=null&&fresh){
                if(pending.equals("leftx")||pending.equals("lefty")||pending.equals("rightx")||pending.equals("righty"))instruction.setText("Move a stick for this control; buttons cannot replace a complete stick axis.");
                else{int raw=ControllerMapping.rawButton(event.getKeyCode());if(raw>=0)captured("b"+raw,KeyEvent.keyCodeToString(event.getKeyCode()));
                    else instruction.setText("This key is not exposed as a controller button. Try another button.");}
            }
        }return true;
    }
    @Override public boolean dispatchGenericMotionEvent(MotionEvent event){
        InputDevice d=event.getDevice();if(!active||!controller(d)||selected==null||!selected.equals(ControllerProfiles.identity(d))||!event.isFromSource(InputDevice.SOURCE_JOYSTICK))return super.dispatchGenericMotionEvent(event);
        List<InputDevice.MotionRange> ranges=new ArrayList<>();
        for(InputDevice.MotionRange r:d.getMotionRanges())if((r.getSource()&InputDevice.SOURCE_CLASS_JOYSTICK)!=0&&r.getAxis()!=MotionEvent.AXIS_HAT_X&&r.getAxis()!=MotionEvent.AXIS_HAT_Y)ranges.add(r);
        ranges.sort((a,b)->Integer.compare(ControllerMapping.axisOrder(a.getAxis()),ControllerMapping.axisOrder(b.getAxis())));
        StringBuilder display=new StringBuilder();double best=0.5;int bestIndex=-1;double base=0,pressed=0;
        for(int i=0;i<ranges.size();i++){
            InputDevice.MotionRange r=ranges.get(i);if(r.getRange()<=0)continue;
            double value=Math.max(-1,Math.min(1,(event.getAxisValue(r.getAxis())-r.getMin())/r.getRange()*2-1));
            display.append(MotionEvent.axisToString(r.getAxis())).append(String.format(Locale.ROOT," %.2f  ",value));
            double neutral=r.getMin()>=0?-1:0,initial=rest.getOrDefault(r.getAxis(),neutral),delta=Math.abs(value-initial);
            if(delta>best&&Math.abs(value)>0.6){best=delta;bestIndex=i;base=initial;pressed=value;}
            latest.put(r.getAxis(),value);
            if(Math.abs(value-neutral)<0.2&&pending!=null)rest.put(r.getAxis(),value);
        }
        showLive(display.toString());
        seenMotion=true;hatX=event.getAxisValue(MotionEvent.AXIS_HAT_X);hatY=event.getAxisValue(MotionEvent.AXIS_HAT_Y);
        if(pending!=null){
            if(!motionPrimed){rest.clear();rest.putAll(latest);restHatX=hatX;restHatY=hatY;motionPrimed=true;instruction.setText("Release controls, then repeat the requested movement.");return true;}
            boolean stick=pending.equals("leftx")||pending.equals("lefty")||pending.equals("rightx")||pending.equals("righty");
            if(!stick){float hx=hatX,hy=hatY;
                boolean x=Math.abs(hx)>0.5&&Math.abs(hx-restHatX)>0.5,y=Math.abs(hy)>0.5&&Math.abs(hy-restHatY)>0.5;
                if(x||y){int raw=x?(hx<0?13:14):(hy<0?11:12);captured("b"+raw,"D-pad "+(raw==11?"Up":raw==12?"Down":raw==13?"Left":"Right"));return true;}}
            if(bestIndex>=0){String expression=ControllerMapping.axisExpression(bestIndex,base,pressed,stick,pending.endsWith("trigger"));
                if(expression!=null)captured(expression,MotionEvent.axisToString(ranges.get(bestIndex).getAxis())+(expression.endsWith("~")?" · inverted":""));}
            if(Math.abs(hatX)<0.3)restHatX=0;if(Math.abs(hatY)<0.3)restHatY=0;
        }return true;
    }
}
