package dev.wiids.xd;

import java.util.*;

/** Immutable value contract for Claude. Unknown effectiveness/objective remains explicitly unknown. */
public final class XdState {
    public static final int WORDS=8464, TEXT=48;
    public static final int SELECT=1,CONFIRM=2,CANCEL=3,UP=4,DOWN=5,LEFT=6,RIGHT=7,
            MOVE=8,PARTY_MEMBER=9,SNAG=10,RUN=11,BAG=12,PARTY=13,PDA=14;
    public static final int UNKNOWN=0,FIELD=1,ROOT=2,PARTY_SCREEN=3,BATTLE_SCREEN=4,COMMAND_SCREEN=5,MOVES_SCREEN=6,TARGET_SCREEN=7,COMPAT=8;
    public final int screenKind,parentMenu,parentKind,currentRow,currentItem,transition,partySlot,partyContext;
    public final boolean visible,lowerOwner;
    public final int[] levels;
    public final long presentationFrame,cleanFrame;
    public final boolean available,battle,fieldMenu,objectiveKnown;
    public final int menuId,phase,mapId,storyProgress,actor,target;
    public final long epoch,sequence,capturedMs;
    public final List<Pokemon> party;
    public final List<Move> moves;
    public final List<Shadow> shadows;
    public final List<Choice> choices;
    public final Pokemon acting,targeted;
    public final int actorPartySlot,targetIndex;
    public final String mapName,objective;
    private XdState(int[] p) {
        screenKind=p[8444];parentMenu=p[8445];currentRow=p[8446];currentItem=p[8447];visible=p[8448]==1;
        transition=p[8449];partySlot=p[8450];partyContext=p[8451];levels=Arrays.copyOfRange(p,8452,8458);
        presentationFrame=wide(p,8458);cleanFrame=wide(p,8460);lowerOwner=p[8462]==1;parentKind=p[8463];
        available=p[3]==1;battle=p[4]==1;fieldMenu=p[5]==1;menuId=p[6];phase=p[7];
        mapId=p[10];storyProgress=p[11];objectiveKnown=p[12]==1;actor=p[13];target=p[14];
        epoch=wide(p,16);sequence=wide(p,18);capturedMs=wide(p,20);
        ArrayList<Pokemon> a=new ArrayList<>();ArrayList<Move> m=new ArrayList<>();
        ArrayList<Shadow> s=new ArrayList<>();ArrayList<Choice> c=new ArrayList<>();
        int b=24;for(int i=0;i<6;i++,b+=55)a.add(new Pokemon(p,b));
        for(int i=0;i<4;i++,b+=55)m.add(new Move(p,b,text(p,8252+i*TEXT),(p[15]>>>(i*8))&255));
        for(int i=0;i<128;i++,b+=5)if(i<p[9])s.add(new Shadow(p,b));
        for(int i=0;i<128;i++,b+=53)if(i<p[8])c.add(new Choice(p,b,available));
        party=Collections.unmodifiableList(a);moves=Collections.unmodifiableList(m);
        shadows=Collections.unmodifiableList(s);choices=Collections.unmodifiableList(c);
        acting=new Pokemon(p,b);targeted=new Pokemon(p,b+55);b+=110;
        mapName=text(p,b);b+=TEXT;objective=text(p,b,96);
        actorPartySlot=p[22];targetIndex=p[23];
    }
    /** Reuse owned presentation values when only publication counters/timestamps change. */
    static final class Decoder {
        private int[] previous;
        private XdState cached;
        private boolean previousStale;
        XdState read(int[] packet,long nowMs){
            if(packet==null||packet.length!=WORDS){previous=null;return unavailable();}
            long age=nowMs-wide(packet,20);boolean stale=age<0||age>250;
            boolean same=previous!=null&&stale==previousStale;
            if(same)for(int i=0;i<WORDS;i++){
                if((i>=16&&i<22)||(i>=8458&&i<8462))continue;
                if(packet[i]!=previous[i]){same=false;break;}
            }
            if(same)return cached;
            cached=decode(packet,nowMs);previous=packet;previousStale=stale;return cached;
        }
    }
    public static XdState decode(int[] packet,long nowMs) {
        if(packet==null||packet.length!=WORDS)return unavailable();
        int[] p=packet.clone();
        if(p[0]!=0x58444350||p[1]!=3||p[2]!=WORDS||p[3]<0||p[3]>1||p[4]<0||p[4]>1||
           p[5]<0||p[5]>1||p[8]<0||p[8]>128||p[9]<0||p[9]>128||p[8444]<UNKNOWN||p[8444]>COMPAT||p[8448]<0||p[8448]>1||
           p[8449]<0||p[8449]>2||p[8450]<-1||p[8450]>6||p[8462]<0||p[8462]>1||p[8463]<0||p[8463]>COMPAT)return unavailable();
        for(int i=8452;i<8458;i++)if(p[i]<0||p[i]>100)return unavailable();
        long age=nowMs-wide(p,20);
        if(age<0||age>250){p[3]=0;p[8448]=0;}
        int choices=24+6*55+4*55+128*5;
        for(int i=0;i<p[8];i++) {
            int b=choices+i*53;
            if(p[b]<SELECT||p[b]>PDA||p[b+4]<0||p[b+4]>1)return unavailable();
        }
        return new XdState(p);
    }
    public static XdState unavailable() {int[] p=new int[WORDS];p[0]=0x58444350;p[1]=3;p[2]=WORDS;return new XdState(p);}
    public boolean allows(int action,int argument) {for(Choice c:choices)if(c.action==action&&c.argument==argument&&c.enabled)return true;return false;}
    private static long wide(int[] p,int b) {return (p[b]&0xffffffffL)|((long)p[b+1]<<32);}
    private static String text(int[] p,int b) {return text(p,b,TEXT);}
    private static String text(int[] p,int b,int count) {StringBuilder s=new StringBuilder();for(int i=0;i<count&&p[b+i]!=0;i++){int c=p[b+i];if(c>=32&&c<=0xffff)s.append((char)c);}return s.toString();}
    public static final class Pokemon {
        public final int species,hp,maxHp,status,shadowId,heart,heartMax;public final String name;
        Pokemon(int[] p,int b){species=p[b];hp=p[b+1];maxHp=p[b+2];status=p[b+3];shadowId=p[b+4];heart=p[b+5];heartMax=p[b+6];name=text(p,b+7);}
    }
    public static final class Move {
        public final int id,type,pp,maxPp,effectivenessNumerator,effectivenessDenominator,effectivenessInfo;public final boolean legal;public final String name,typeName;
        Move(int[] p,int b,String typeName,int info){id=p[b];type=p[b+1];boolean shadow="SHADOW".equalsIgnoreCase(typeName);pp=shadow?-1:p[b+2];maxPp=shadow?-1:p[b+3];legal=p[b+4]==1;effectivenessNumerator=p[b+5];effectivenessDenominator=p[b+6];name=text(p,b+7);this.typeName=typeName;effectivenessInfo=info;}
        public String ppLabel(){
            if("SHADOW".equalsIgnoreCase(typeName))return "PP — / —";
            return pp<0||maxPp<=0||pp>maxPp?"PP not available":pp+" / "+maxPp+" PP";
        }
        public String matchLabel(){
            if(effectivenessInfo==3)return "blocked by ability";
            if(effectivenessInfo==4)return "already has status";
            if(effectivenessInfo==5)return "status blocked by type";
            if(effectivenessInfo==6)return "blocked by Substitute or Safeguard";
            if(effectivenessInfo==7)return "sleep blocked by Uproar";
            if(effectivenessInfo==8)return "blocked by battle rule";
            if(effectivenessInfo==2)return "Status move";
            if(effectivenessNumerator>=0&&effectivenessDenominator>0){
                double value=(double)effectivenessNumerator/effectivenessDenominator;
                if(value==0)return "No effect (0×)";
                String multiplier=new java.math.BigDecimal(effectivenessNumerator).divide(new java.math.BigDecimal(effectivenessDenominator),2,java.math.RoundingMode.HALF_UP).stripTrailingZeros().toPlainString()+"×";
                return (value>1?"Super effective":value<1?"Not very effective":"Normal effectiveness")+" ("+multiplier+")";
            }
            return "Effectiveness unknown";
        }
    }
    public static final class Shadow {
        public final int id,species,heart;public final boolean snagged,missed;
        Shadow(int[] p,int b){id=p[b];species=p[b+1];snagged=p[b+2]==1;missed=p[b+3]==1;heart=p[b+4];}
    }
    public static final class Choice {
        public final int action,argument,itemId,cursor;public final boolean enabled;public final String label;
        Choice(int[] p,int b,boolean available){action=p[b];argument=p[b+1];itemId=p[b+2];cursor=p[b+3];enabled=p[b+4]==1;label=text(p,b+5);}
    }
}
