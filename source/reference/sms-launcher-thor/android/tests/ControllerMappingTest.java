package dev.chasem.smslauncher;

final class ControllerMappingTest {
    private static void check(boolean ok){if(!ok)throw new AssertionError();}
    public static void main(String[] args){
        check(ControllerMapping.descriptorCrc("123456789")==0xbb3d);
        check(ControllerMapping.descriptorCrc("")==0);
        check(ControllerMapping.descriptorCrc("\u0000")==0xa051);
        check(ControllerMapping.descriptorCrc("\ud83d\ude00")==0x9d50);
        for(int i=0;i<16;i++)check(ControllerMapping.rawButton(188+i)==20+i);
        check(ControllerMapping.rawButton(96)==0&&ControllerMapping.rawButton(103)==10);
        check(ControllerMapping.rawButton(4)==4&&ControllerMapping.rawButton(108)==6);
        check(ControllerMapping.rawButton(19)==11&&ControllerMapping.rawButton(22)==14);
        check(ControllerMapping.rawButton(0)==-1&&ControllerMapping.rawButton(29)==-1);
        Integer[] sorted={0,1,11,12,13,14};
        java.util.Arrays.sort(sorted,(a,b)->Integer.compare(ControllerMapping.axisOrder(a),ControllerMapping.axisOrder(b)));
        check(java.util.Arrays.equals(sorted,new Integer[]{0,1,12,13,11,14}));
        check(ControllerMapping.axisExpression(3,0,1,true,false).equals("a3"));
        check(ControllerMapping.axisExpression(3,0,-1,true,false).equals("a3~"));
        check(ControllerMapping.axisExpression(2,-1,1,false,true).equals("a2"));
        check(ControllerMapping.axisExpression(2,1,-1,false,true).equals("a2~"));
        check(ControllerMapping.axisExpression(2,0,1,false,true).equals("+a2"));
        check(ControllerMapping.axisExpression(2,0,-1,false,true).equals("-a2"));
        check(ControllerMapping.axisExpression(2,0,0.2,false,true)==null);
        check(ControllerMapping.axisExpression(64,0,1,true,false)==null);
        check(ControllerMapping.axisExpression(2,0,Double.NaN,true,false)==null);
        ControllerMapping map=new ControllerMapping();map.set("a","b22","BUTTON_3");map.set("rightx","a2~","AXIS_Z");map.set("y","none","Unassigned");
        check(map.entries().equals("a:b22,y:none,rightx:a2~,"));
        check(map.labels.get("a").equals("BUTTON_3"));
        for(String value:new String[]{"b36","b-1","a64","h0.1","+b1","a1,crc:1234","","a9999","b1~"})check(!ControllerMapping.expression(value));
        for(String value:new String[]{"b0","b35","a63","+a3","-a3~","none"})check(ControllerMapping.expression(value));
        boolean failed=false;try{map.set("crc","b0","bad");}catch(IllegalArgumentException e){failed=true;}check(failed);
        System.out.println("PASS controller configuration: Android button ABI, SDL axis order, inversion, trigger calibration, descriptor identity and bounded mapping serialization");
    }
}
