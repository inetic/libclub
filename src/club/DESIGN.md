# This document should describe how and why Rooms works:

## The ideal case

There are N mates and everyone is connected to everyone (full graph) and they
all send and receive data in parralel. The purpose of Rooms is to make sure that
everyone will receive messages in the same order while preserving causal
relationships of the messages. That is, the following properties must hold:

  T1) If a mate sends two messages M1 and M2 in that order, then everyone
      will receive these in the same order.

  T2) If mate B sends message M2 after receiving message M1 from mate A, then
      everyone will receive these messages in order: M1 ->...-> M2

This is achieved using vector clocks and these are the consequences of T1 and T2:

  C1) Everytime I want to send a message to the system, I need to increment
      my clock and add it to the message.

  C2) Everytime I receive a message A, I need to update my clock with sender's
      clock so that next time I send another message it will reflect the fact
      that it was created after A.

  C3) I can't act uppon receiving the message right away as they may come
      in different orders to different mates. Of course, if mate A sends
      messages M1 -> M2 to mate B, they will arrive in the correct order
      (due to how sockets work), but say mates A1 and A2 each want to send a
      single message to B1 and B2, then B1 may receive messages
      in order MA1 then MA2 and B2 may receive the messages in order
      MA2 then MA1. 

      Due to this, we need to store the messages in a sorted buffer with
      the following sorting function:
         (M1 < M2) = case
                       (M1 -> M2) then return true
                       (M2 -> M1) then return false
                       return (Sender of M1 has smaller UUID thatn sender of M2)
                     
... TODO ...

TODO: Explain how rooms are merged.

## Advanced cases

### [ME-DEAD] Should I send (DeadMate myself) when I die?
No. Socket API lets others know that I'm dead, either they receive a CLOSE
packet or the KeepAlive kicks in.

### [NEW-DEAD] When NewMates is received, some of the new mates may be
### dead from prevous executions, what to do with them?
TODO: Not resolved yet.
They can't be ignored (not added), because some mates might have them
marked as dead, and some might have them already removed. So the former
group would not add them, while the later group would add them => Inconsistent
room.

Two approaches come to my mind:
  1) Add them, and then send DeadMate... but this seems to have the same
     problems as 2)
  2) Resurect the dead mate. This would require:
       * Handle the awaiting removal commit.
       * Handle canceled send and receive operations inside Mate

### [DEAD-COMMIT] May I commit a log entry from a mate whom I've got
### marked as dead?
No. If I've got him marked as dead and have a log entry from him,
someone else might not have that entry. But if that is the case, I couln't
have been in the state where I was able to commit the message in the first
place (because the mate who doesn't have the message cound't have possibly
sent me an ACK).

But I still may have the entry in the log, the resolution is that commits
from dead mates are ignored and removed from the log.

### [OMIT-DEAD] Should clocks of my outgoing messages contain versions
### of dead mates?
No. Not sending the dead mate's version is important when we want to forget
about removed mates (so that the clock wont grow forever). But if I just
sent a message without the version, then the receiver would think that
the message is old and ignore it in the on\_recv function.

This can be resolved by first sending DeadMate message *before* I send
any other message with the dead mate's version omited from its clock.
Also, after the receiver receives our DeadMate message, he must exclude
versions from dead mates from its clock when deciding if the message is
old or not in its on\_recv function.
