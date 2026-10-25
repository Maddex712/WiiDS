#include "TrackpadMailbox.h"
#include "port/camera_stick.h"
#include <cassert>
#include <cstdio>
#include <limits>
#include <thread>
int main() {
 int checks=0;
 auto ok=[&](bool v){ ++checks; if(!v){printf("FAIL %d\n",checks);abort();} };
 using M=mobile_thor::TrackpadMailbox; M m;float x=.6f,y=-.4f;
 m.send(0,0,M::Begin);ok(m.apply(x,y)&&x==.6f&&y==-.4f);
 m.send(.2f,.1f,M::Move);ok(m.apply(x,y)&&x>.79f&&y<-.29f);
 m.send(0,0,M::End);ok(!m.apply(x,y));float last=x;
 m.send(0,0,M::Begin);ok(m.apply(x,y)&&x==last); // reposition starts at shared cursor
 x=.9f;y=0;m.send(.5f,0,M::Move);m.send(-.2f,0,M::Move);m.send(0,0,M::End);
 ok(m.apply(x,y)&&x>.799f&&x<.801f);ok(!m.apply(x,y)); // ordered edge reversal
 m.send(0,0,M::Begin);m.send(.2f,0,M::Move);m.send(0,0,M::End);
 m.send(0,0,M::Begin);m.send(-.3f,0,M::Move);m.send(0,0,M::End);
 ok(m.apply(x,y)&&x>.699f&&x<.701f); // both gestures between two input frames
 m.send(0,0,M::Begin);m.send(1,1,M::Move);m.clear();ok(!m.apply(x,y)&&x<.701f);
 m.send(1,1,M::Move);ok(!m.apply(x,y));
 m.send(0,0,M::Begin);m.send(std::numeric_limits<float>::quiet_NaN(),0,M::Move);ok(!m.apply(x,y));
 m.send(0,0,M::Begin);for(int i=0;i<257;i++)m.send(.1f,0,M::Move);ok(!m.apply(x,y));
 m.send(0,0,M::Begin);m.send(0,0,42);ok(!m.apply(x,y));
 m.send(0,0,M::Begin);m.send(100,-100,M::Move);ok(m.apply(x,y)&&x==1&&y==-1);
 m.send(0,0,M::Cancel);ok(!m.apply(x,y));
 // R3/right-stick handoff changes the shared anchor, without a stale Java coordinate.
 x=0;y=0;m.send(0,0,M::Begin);m.send(.1f,.2f,M::Move);ok(m.apply(x,y)&&x==.1f&&y==.2f);
 m.clear();std::thread producer([&]{for(int i=0;i<100000;i++){m.send(0,0,M::Begin);m.send(.1f,-.1f,M::Move);m.send(0,0,M::End);}});
 for(int i=0;i<100000;i++){m.apply(x,y);ok(std::isfinite(x)&&std::isfinite(y)&&x>=-1&&x<=1&&y>=-1&&y<=1);}producer.join();m.clear();
 port::CameraStick c;c.update(0,true);ok(!c.left()&&!c.right());
 c.update(1,true);ok(c.right()&&!c.left());ok(c.right()); // multiple same-frame readers
 for(int i=0;i<120;i++){c.update(1,true);ok(!c.left()&&!c.right());}
 c.update(.3f,true);ok(!c.right());c.update(0,true);c.update(-1,true);ok(c.left());
 c.update(1,false);ok(!c.right());c.update(1,true);ok(!c.right()); // blocked cutscene cannot queue a turn
 c.update(0,false);c.update(1,true);ok(c.right());
 c.update(std::numeric_limits<float>::quiet_NaN(),true);ok(!c.left()&&!c.right());
 printf("%d actual native mailbox/camera checks PASS\n",checks);
}
