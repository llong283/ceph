
#include "include/types.h"

#include "OSD.h"

#include "FakeStore.h"

#include "msg/Messenger.h"
#include "msg/Message.h"

#include "messages/MPing.h"
#include "messages/MOSDRead.h"
#include "messages/MOSDReadReply.h"
#include "messages/MOSDWrite.h"
#include "messages/MOSDWriteReply.h"
#include "messages/MOSDOp.h"
#include "messages/MOSDOpReply.h"

#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/file.h>
#include <iostream>
#include <cassert>
#include <errno.h>

#include "include/config.h"
#undef dout
#define  dout(l)    if (l<=g_conf.debug) cout << "osd" << whoami << " "

char *osd_base_path = "./osddata";

// cons/des

OSD::OSD(int id, Messenger *m) 
{
  whoami = id;
  messenger = m;

  // use fake store
  store = new FakeStore(osd_base_path, whoami);
}

OSD::~OSD()
{
  if (messenger) { delete messenger; messenger = 0; }
}

int OSD::init()
{
  messenger->set_dispatcher(this);
  return 0;
}

int OSD::shutdown()
{
  messenger->shutdown();
  return 0;
}



// dispatch

void OSD::dispatch(Message *m) 
{
  switch (m->get_type()) {
  
  case MSG_SHUTDOWN:
	shutdown();
	break;

  case MSG_PING:
	handle_ping((MPing*)m);
	break;
	
  case MSG_OSD_READ:
	read((MOSDRead*)m);
	break;

  case MSG_OSD_WRITE:
	write((MOSDWrite*)m);
	break;

  case MSG_OSD_OP:
	handle_op((MOSDOp*)m);
	break;

  default:
	dout(1) << " got unknown message " << m->get_type() << endl;
  }

  delete m;
}


void OSD::handle_ping(MPing *m)
{
  // play dead?
  if (whoami == 3) {
	dout(7) << "got ping, replying" << endl;
	messenger->send_message(new MPing(0),
							m->get_source(), m->get_source_port(), 0);
  } else {
	dout(7) << "playing dead" << endl;
  }

  delete m;
}



// -- osd_read



void OSD::handle_op(MOSDOp *op)
{
  switch (op->get_op()) {
  case OSD_OP_DELETE:
	{
	  int r = store->destroy(op->get_oid());
	  dout(3) << "delete on " << op->get_oid() << " r = " << r << endl;
	  
	  // "ack"
	  messenger->send_message(new MOSDOpReply(op, r), 
							  op->get_source(), op->get_source_port());
	}
	break;

  case OSD_OP_STAT:
	{
	  struct stat st;
	  memset(&st, sizeof(st), 0);
	  int r = store->stat(op->get_oid(), &st);
  
	  dout(3) << "stat on " << op->get_oid() << " r = " << r << " size = " << st.st_size << endl;
	  
	  MOSDOpReply *reply = new MOSDOpReply(op, r);
	  reply->set_size(st.st_size);
	  messenger->send_message(reply,
							  op->get_source(), op->get_source_port());
	}
	
  default:
	assert(0);
  }
}




void OSD::read(MOSDRead *r)
{
  MOSDReadReply *reply;

  if (!store->exists(r->get_oid())) {
	// send reply (failure)
	dout(1) << "read open FAILED on " << r->get_oid() << endl;
	reply = new MOSDReadReply(r, -1);
	//assert(0);
  }

  // create reply, buffer
  reply = new MOSDReadReply(r, r->get_len());

  // read into a buffer
  char *buf = reply->get_buffer();
  long got = store->read(r->get_oid(), 
						 r->get_len(), r->get_offset(),
						 buf);
  reply->set_len(got);

  dout(10) << "osd_read " << got << " / " << r->get_len() << " bytes from " << r->get_oid() << endl;

  // send it
  messenger->send_message(reply, r->get_source(), r->get_source_port());
}


// -- osd_write

void OSD::write(MOSDWrite *m)
{

  int r = store->write(m->get_oid(),
					   m->get_len(), m->get_offset(),
					   m->get_buffer());
  if (r < 0) {
	assert(2+2==5);
  }
  
  // clean up
  MOSDWriteReply *reply = new MOSDWriteReply(m, r);

  messenger->send_message(reply, m->get_source(), m->get_source_port());
}

