c-p2p-chat
==========   
   
P2P chat application written in C, based on the work of:

Charles (Ben) Schmaltz, Alexander Leavitt, Yiqi Chen

How to build & run
------------------

run make to compile the code

to run tracker program, it takes in one optional parameter: 

  ./tracker %tracker_port%

  This will create two UDP sockets at ports %tracker_port% and %tracker_port+1%.
  If %tracker_port% is not specified, it uses 8080. So, the ports used are 8080 and 8081

to run peer program, it takes in 3 parameters: 

  ./peer %tracker_ip% %tracker_port% %peer_port%
  
  
How to use:
-----------

Peer program takes user inputs, and below is the input format:

* To request a list of all available rooms and number of peers in each room: 
  
  -r

* To request to create a new room: 

  -c

* To request to join or switch to a new room: 

  -j %new_chatroom_number%

* To request to leave a room

  -l

* To send a message to peers in the chatroom: 

  -m %message_that_you_want_to_send%
  
* To set nickname:

  -n %nickname%
