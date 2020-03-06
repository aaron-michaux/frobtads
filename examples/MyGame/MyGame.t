
#include <adv3.h>
#include <en_us.h>

gameMain : GameMainDef initialPlayerChar = me;

versionInfo
    : GameID name
      = 'My First Game' byline
      = 'by Bob Author' authorEmail
      = 'Bob Author <bob@myisp.com>' desc
      = 'This is an example of how to start a new game project. ' version
      = '1' IFID
      = 'b8563851-6257-77c3-04ee-278ceaeb48ac';

firstRoom : Room 'Starting Room' "This is the boring starting room.";

+ me:Actor;