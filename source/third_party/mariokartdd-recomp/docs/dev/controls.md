# Controls

## Where the sticks respond

The karts never see the pad's stick as it is. `JUTGamePad` reads the pad and, in the
clamp mode the game leaves set (1), calls the SDK's `PADClamp`, with the SDK's standard
region: 15 is taken off each axis of the control stick and the result capped at 72. Then
`PadMgr::processKartPad` packs every pad through `KartGamePad::compress` (for the ghost
recorder) and the karts drive from what `KartGamePad::expand` unpacks: x divided by 4 and
y by 18, truncated, x held to +-15 and y to +-3.

So steering does nothing until the stick is 19 from the centre, then has 15 steps each
way, the last reached at 75; up and down has 3, from 33 to 69. A wheel mapped onto the
stick's whole travel lost the first fifth of every turn to nothing and reached full lock a
quarter short of the binding's (and the force feedback's) full lock. `src/controls.cpp`
gives controls.txt these ranges (`stick_x_min` 19, `stick_x_max` 75, `stick_y_` 33 and
69), so a bound axis is spread across exactly the steps the game has.

Found by reading the code (`compress__11KartGamePadFRC9PADStatusP11KartPadData` and the
clamp mode's initial value); not yet measured in a race.
