package dev.wiids.xd;
import android.content.Context;
import android.view.View;
import android.widget.FrameLayout;
public final class XdCompanionView extends FrameLayout {
    private XdState state=XdState.unavailable();
    private XdState fieldState=XdState.unavailable();
    private final XdBoard board;
    private String lastFieldLocation="";
    public XdCompanionView(Context context){
        super(context);inert(this);board=new XdBoard(context);inert(board);
        for(int i=0;i<6;i++){XdKey k=new XdKey(context,XdKey.PARTY);inert(k);board.party[i]=k;board.addView(k);}
        addView(board,new FrameLayout.LayoutParams(-1,-1));
    }
    private static void inert(View v){v.setClickable(false);v.setLongClickable(false);v.setFocusable(false);v.setFocusableInTouchMode(false);v.setSoundEffectsEnabled(false);v.setHapticFeedbackEnabled(false);}
    public void setMenuOpen(boolean value){}
    public void fieldColor(int color){board.fieldColor(color);}
    public void render(XdState value){
        if(value==state)return;state=value;
        if(value.visible&&value.screenKind==XdState.FIELD)fieldState=value;
        else if(value.lowerOwner&&fieldState.visible)value=fieldState;
        // Keep the last field device ready behind the fully opaque native menu.
        // Closing the native layer reveals it in the same frame, without polling
        // a second transition or leaving party plates visible around the menu.
        boolean field=value.visible&&value.screenKind==XdState.FIELD;
        int mode=field?XdBoard.FIELD:XdBoard.WAITING;
        if(field&&!value.mapName.isEmpty())lastFieldLocation=value.mapName;
        board.location=field?lastFieldLocation:"";board.prompt="";board.tag="";
        for(int i=0;i<6;i++){
            XdState.Pokemon p=value.party.get(i);XdKey k=board.party[i];
            k.setText(p.species==0?"":p.name);k.bindMon(p,false,false);
            int visibility=field&&p.species!=0?View.VISIBLE:View.GONE;
            if(k.getVisibility()!=visibility)k.setVisibility(visibility);
        }
        if(board.mode!=mode){board.mode=mode;board.requestLayout();}
        board.bind(value);
    }
}
