#ifndef ENGINE_FRAME_QUEUE_H
#define ENGINE_FRAME_QUEUE_H

// Destroys the frame queue's queues (docs/ENGINE.md), giving back the memory
// a heavy scene grew them to: each keeps the largest frame it has recorded.
// Waits for the RSP; not while a frame records. The scene manager calls it
// when it switches to another scene.
void engine_frame_queue_release(void);

#endif
