#include "funkotto/etherlink_usb.h"
#include "funkotto/raw_wire.h"
void fo_el_stream_clear(struct fo_el_stream *w) { fo_wipe(w,sizeof(*w)); }
void fo_el_stream_expire(struct fo_el_stream *w) {
    fo_wipe(w->input,sizeof(w->input)); w->used=0; w->discarding=true;
}
size_t fo_el_stream_feed(struct fo_el_stream *w, struct fo_el_server *s,
                         uint8_t byte, uint8_t out[FO_EL_ENCODED]) {
    if(byte) {
        if(!w->discarding) {
            if(w->used<sizeof(w->input)) w->input[w->used++]=byte;
            else fo_el_stream_expire(w);
        }
        return 0;
    }
    if(!w->used && !w->discarding) return 0;
    size_t n=w->discarding ? 0 : fo_cobs_decode(w->input,w->used,w->decoded,sizeof(w->decoded));
    size_t reply=n ? fo_el_request(s,w->decoded,n,w->reply) : 0;
    size_t count=reply ? fo_cobs_encode(w->reply,reply,out,FO_EL_ENCODED) : 0;
    if(!count) ++w->bad_frames;
    w->used=0; w->discarding=false;
    fo_wipe(w->input,sizeof(w->input)); fo_wipe(w->decoded,sizeof(w->decoded));
    fo_wipe(w->reply,sizeof(w->reply)); return count;
}
