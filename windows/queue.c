/* * Message queues related functions
 *
 * Copyright 1993, 1994 Alexandre Julliard
 */

#include <string.h>
#include <signal.h>
#include <assert.h>
#include "windef.h"
#include "wingdi.h"
#include "winerror.h"
#include "wine/winbase16.h"
#include "wine/winuser16.h"
#include "queue.h"
#include "task.h"
#include "win.h"
#include "hook.h"
#include "heap.h"
#include "thread.h"
#include "debugtools.h"
#include "server.h"
#include "spy.h"

DECLARE_DEBUG_CHANNEL(msg);
DECLARE_DEBUG_CHANNEL(sendmsg);

#define MAX_QUEUE_SIZE   120  /* Max. size of a message queue */

static HQUEUE16 hFirstQueue = 0;
static HQUEUE16 hExitingQueue = 0;
static HQUEUE16 hmemSysMsgQueue = 0;
static MESSAGEQUEUE *sysMsgQueue = NULL;
static PERQUEUEDATA *pQDataWin16 = NULL;  /* Global perQData for Win16 tasks */

static MESSAGEQUEUE *pMouseQueue = NULL;  /* Queue for last mouse message */
static MESSAGEQUEUE *pKbdQueue = NULL;    /* Queue for last kbd message */

HQUEUE16 hCursorQueue = 0;
HQUEUE16 hActiveQueue = 0;


/***********************************************************************
 *           PERQDATA_CreateInstance
 *
 * Creates an instance of a reference counted PERQUEUEDATA element
 * for the message queue. perQData is stored globally for 16 bit tasks.
 *
 * Note: We don't implement perQdata exactly the same way Windows does.
 * Each perQData element is reference counted since it may be potentially
 * shared by multiple message Queues (via AttachThreadInput).
 * We only store the current values for Active, Capture and focus windows
 * currently.
 */
PERQUEUEDATA * PERQDATA_CreateInstance( )
{
    PERQUEUEDATA *pQData;
    
    BOOL16 bIsWin16 = 0;
    
    TRACE_(msg)("()\n");

    /* Share a single instance of perQData for all 16 bit tasks */
    if ( ( bIsWin16 = THREAD_IsWin16( NtCurrentTeb() ) ) )
    {
        /* If previously allocated, just bump up ref count */
        if ( pQDataWin16 )
        {
            PERQDATA_Addref( pQDataWin16 );
            return pQDataWin16;
        }
    }

    /* Allocate PERQUEUEDATA from the system heap */
    if (!( pQData = (PERQUEUEDATA *) HeapAlloc( SystemHeap, 0,
                                                    sizeof(PERQUEUEDATA) ) ))
        return 0;

    /* Initialize */
    pQData->hWndCapture = pQData->hWndFocus = pQData->hWndActive = 0;
    pQData->ulRefCount = 1;
    pQData->nCaptureHT = HTCLIENT;

    /* Note: We have an independent critical section for the per queue data
     * since this may be shared by different threads. see AttachThreadInput()
     */
    InitializeCriticalSection( &pQData->cSection );
    /* FIXME: not all per queue data critical sections should be global */
    MakeCriticalSectionGlobal( &pQData->cSection );

    /* Save perQData globally for 16 bit tasks */
    if ( bIsWin16 )
        pQDataWin16 = pQData;
        
    return pQData;
}


/***********************************************************************
 *           PERQDATA_Addref
 *
 * Increment reference count for the PERQUEUEDATA instance
 * Returns reference count for debugging purposes
 */
ULONG PERQDATA_Addref( PERQUEUEDATA *pQData )
{
    assert(pQData != 0 );
    TRACE_(msg)("(): current refcount %lu ...\n", pQData->ulRefCount);

    EnterCriticalSection( &pQData->cSection );
    ++pQData->ulRefCount;
    LeaveCriticalSection( &pQData->cSection );

    return pQData->ulRefCount;
}


/***********************************************************************
 *           PERQDATA_Release
 *
 * Release a reference to a PERQUEUEDATA instance.
 * Destroy the instance if no more references exist
 * Returns reference count for debugging purposes
 */
ULONG PERQDATA_Release( PERQUEUEDATA *pQData )
{
    assert(pQData != 0 );
    TRACE_(msg)("(): current refcount %lu ...\n",
          (LONG)pQData->ulRefCount );

    EnterCriticalSection( &pQData->cSection );
    if ( --pQData->ulRefCount == 0 )
    {
        LeaveCriticalSection( &pQData->cSection );
        DeleteCriticalSection( &pQData->cSection );

        TRACE_(msg)("(): deleting PERQUEUEDATA instance ...\n" );

        /* Deleting our global 16 bit perQData? */
        if ( pQData == pQDataWin16 )
            pQDataWin16 = 0;
            
        /* Free the PERQUEUEDATA instance */
        HeapFree( SystemHeap, 0, pQData );

        return 0;
    }
    LeaveCriticalSection( &pQData->cSection );

    return pQData->ulRefCount;
}


/***********************************************************************
 *           PERQDATA_GetFocusWnd
 *
 * Get the focus hwnd member in a threadsafe manner
 */
HWND PERQDATA_GetFocusWnd( PERQUEUEDATA *pQData )
{
    HWND hWndFocus;
    assert(pQData != 0 );

    EnterCriticalSection( &pQData->cSection );
    hWndFocus = pQData->hWndFocus;
    LeaveCriticalSection( &pQData->cSection );

    return hWndFocus;
}


/***********************************************************************
 *           PERQDATA_SetFocusWnd
 *
 * Set the focus hwnd member in a threadsafe manner
 */
HWND PERQDATA_SetFocusWnd( PERQUEUEDATA *pQData, HWND hWndFocus )
{
    HWND hWndFocusPrv;
    assert(pQData != 0 );

    EnterCriticalSection( &pQData->cSection );
    hWndFocusPrv = pQData->hWndFocus;
    pQData->hWndFocus = hWndFocus;
    LeaveCriticalSection( &pQData->cSection );

    return hWndFocusPrv;
}


/***********************************************************************
 *           PERQDATA_GetActiveWnd
 *
 * Get the active hwnd member in a threadsafe manner
 */
HWND PERQDATA_GetActiveWnd( PERQUEUEDATA *pQData )
{
    HWND hWndActive;
    assert(pQData != 0 );

    EnterCriticalSection( &pQData->cSection );
    hWndActive = pQData->hWndActive;
    LeaveCriticalSection( &pQData->cSection );

    return hWndActive;
}


/***********************************************************************
 *           PERQDATA_SetActiveWnd
 *
 * Set the active focus hwnd member in a threadsafe manner
 */
HWND PERQDATA_SetActiveWnd( PERQUEUEDATA *pQData, HWND hWndActive )
{
    HWND hWndActivePrv;
    assert(pQData != 0 );

    EnterCriticalSection( &pQData->cSection );
    hWndActivePrv = pQData->hWndActive;
    pQData->hWndActive = hWndActive;
    LeaveCriticalSection( &pQData->cSection );

    return hWndActivePrv;
}


/***********************************************************************
 *           PERQDATA_GetCaptureWnd
 *
 * Get the capture hwnd member in a threadsafe manner
 */
HWND PERQDATA_GetCaptureWnd( PERQUEUEDATA *pQData )
{
    HWND hWndCapture;
    assert(pQData != 0 );

    EnterCriticalSection( &pQData->cSection );
    hWndCapture = pQData->hWndCapture;
    LeaveCriticalSection( &pQData->cSection );

    return hWndCapture;
}


/***********************************************************************
 *           PERQDATA_SetCaptureWnd
 *
 * Set the capture hwnd member in a threadsafe manner
 */
HWND PERQDATA_SetCaptureWnd( PERQUEUEDATA *pQData, HWND hWndCapture )
{
    HWND hWndCapturePrv;
    assert(pQData != 0 );

    EnterCriticalSection( &pQData->cSection );
    hWndCapturePrv = pQData->hWndCapture;
    pQData->hWndCapture = hWndCapture;
    LeaveCriticalSection( &pQData->cSection );

    return hWndCapturePrv;
}


/***********************************************************************
 *           PERQDATA_GetCaptureInfo
 *
 * Get the capture info member in a threadsafe manner
 */
INT16 PERQDATA_GetCaptureInfo( PERQUEUEDATA *pQData )
{
    INT16 nCaptureHT;
    assert(pQData != 0 );

    EnterCriticalSection( &pQData->cSection );
    nCaptureHT = pQData->nCaptureHT;
    LeaveCriticalSection( &pQData->cSection );

    return nCaptureHT;
}


/***********************************************************************
 *           PERQDATA_SetCaptureInfo
 *
 * Set the capture info member in a threadsafe manner
 */
INT16 PERQDATA_SetCaptureInfo( PERQUEUEDATA *pQData, INT16 nCaptureHT )
{
    INT16 nCaptureHTPrv;
    assert(pQData != 0 );

    EnterCriticalSection( &pQData->cSection );
    nCaptureHTPrv = pQData->nCaptureHT;
    pQData->nCaptureHT = nCaptureHT;
    LeaveCriticalSection( &pQData->cSection );

    return nCaptureHTPrv;
}


/***********************************************************************
 *	     QUEUE_Lock
 *
 * Function for getting a 32 bit pointer on queue structure. For thread
 * safeness programmers should use this function instead of GlobalLock to
 * retrieve a pointer on the structure. QUEUE_Unlock should also be called
 * when access to the queue structure is not required anymore.
 */
MESSAGEQUEUE *QUEUE_Lock( HQUEUE16 hQueue )
{
    MESSAGEQUEUE *queue;

    HeapLock( SystemHeap );  /* FIXME: a bit overkill */
    queue = GlobalLock16( hQueue );
    if ( !queue || (queue->magic != QUEUE_MAGIC) )
    {
        HeapUnlock( SystemHeap );
        return NULL;
    }

    queue->lockCount++;
    HeapUnlock( SystemHeap );
    return queue;
}


/***********************************************************************
 *	     QUEUE_Unlock
 *
 * Use with QUEUE_Lock to get a thread safe access to message queue
 * structure
 */
void QUEUE_Unlock( MESSAGEQUEUE *queue )
{
    if (queue)
    {
        HeapLock( SystemHeap );  /* FIXME: a bit overkill */

        if ( --queue->lockCount == 0 )
        {
            DeleteCriticalSection ( &queue->cSection );
            if (queue->server_queue)
                CloseHandle( queue->server_queue );
            GlobalFree16( queue->self );
        }
    
        HeapUnlock( SystemHeap );
    }
}


/***********************************************************************
 *	     QUEUE_DumpQueue
 */
void QUEUE_DumpQueue( HQUEUE16 hQueue )
{
    MESSAGEQUEUE *pq; 

    if (!(pq = (MESSAGEQUEUE*) QUEUE_Lock( hQueue )) )
    {
        WARN_(msg)("%04x is not a queue handle\n", hQueue );
        return;
    }

    DPRINTF( "next: %12.4x  Intertask SendMessage:\n"
             "thread: %10p  ----------------------\n"
             "firstMsg: %8p   smWaiting:     %10p\n"
             "lastMsg:  %8p   smPending:     %10p\n"
             "msgCount: %8.4x   smProcessing:  %10p\n"
             "lockCount: %7.4x\n"
             "wWinVer: %9.4x\n"
             "paints: %10.4x\n"
             "timers: %10.4x\n"
             "wakeBits: %8.4x\n"
             "wakeMask: %8.4x\n"
             "hCurHook: %8.4x\n",
             pq->next, pq->teb, pq->firstMsg, pq->smWaiting, pq->lastMsg,
             pq->smPending, pq->msgCount, pq->smProcessing,
             (unsigned)pq->lockCount, pq->wWinVersion,
             pq->wPaintCount, pq->wTimerCount,
             pq->wakeBits, pq->wakeMask, pq->hCurHook);

    QUEUE_Unlock( pq );
}


/***********************************************************************
 *	     QUEUE_WalkQueues
 */
void QUEUE_WalkQueues(void)
{
    char module[10];
    HQUEUE16 hQueue = hFirstQueue;

    DPRINTF( "Queue Msgs Thread   Task Module\n" );
    while (hQueue)
    {
        MESSAGEQUEUE *queue = (MESSAGEQUEUE *)QUEUE_Lock( hQueue );
        if (!queue)
        {
            WARN_(msg)("Bad queue handle %04x\n", hQueue );
            return;
        }
        if (!GetModuleName16( queue->teb->htask16, module, sizeof(module )))
            strcpy( module, "???" );
        DPRINTF( "%04x %4d %p %04x %s\n", hQueue,queue->msgCount,
                 queue->teb, queue->teb->htask16, module );
        hQueue = queue->next;
        QUEUE_Unlock( queue );
    }
    DPRINTF( "\n" );
}


/***********************************************************************
 *	     QUEUE_IsExitingQueue
 */
BOOL QUEUE_IsExitingQueue( HQUEUE16 hQueue )
{
    return (hExitingQueue && (hQueue == hExitingQueue));
}


/***********************************************************************
 *	     QUEUE_SetExitingQueue
 */
void QUEUE_SetExitingQueue( HQUEUE16 hQueue )
{
    hExitingQueue = hQueue;
}


/***********************************************************************
 *           QUEUE_CreateMsgQueue
 *
 * Creates a message queue. Doesn't link it into queue list!
 */
static HQUEUE16 QUEUE_CreateMsgQueue( BOOL16 bCreatePerQData )
{
    HQUEUE16 hQueue;
    HANDLE handle = -1;
    MESSAGEQUEUE * msgQueue;
    TDB *pTask = (TDB *)GlobalLock16( GetCurrentTask() );

    TRACE_(msg)("(): Creating message queue...\n");

    if (!(hQueue = GlobalAlloc16( GMEM_FIXED | GMEM_ZEROINIT,
                                  sizeof(MESSAGEQUEUE) )))
        return 0;

    msgQueue = (MESSAGEQUEUE *) GlobalLock16( hQueue );
    if ( !msgQueue )
        return 0;

    SERVER_START_REQ
    {
        struct get_msg_queue_request *req = server_alloc_req( sizeof(*req), 0 );
        if (!server_call( REQ_GET_MSG_QUEUE )) handle = req->handle;
    }
    SERVER_END_REQ;
    if (handle == -1)
    {
        ERR_(msg)("Cannot get thread queue");
        GlobalFree16( hQueue );
        return 0;
    }
    msgQueue->server_queue = handle;
    msgQueue->server_queue = ConvertToGlobalHandle( msgQueue->server_queue );

    msgQueue->self        = hQueue;
    msgQueue->wakeBits    = msgQueue->changeBits = 0;
    msgQueue->wWinVersion = pTask ? pTask->version : 0;
    
    InitializeCriticalSection( &msgQueue->cSection );
    MakeCriticalSectionGlobal( &msgQueue->cSection );

    msgQueue->lockCount = 1;
    msgQueue->magic = QUEUE_MAGIC;
    
    /* Create and initialize our per queue data */
    msgQueue->pQData = bCreatePerQData ? PERQDATA_CreateInstance() : NULL;
    
    return hQueue;
}


/***********************************************************************
 *           QUEUE_FlushMessage
 * 
 * Try to reply to all pending sent messages on exit.
 */
static void QUEUE_FlushMessages( MESSAGEQUEUE *queue )
{
    SMSG *smsg;
    MESSAGEQUEUE *senderQ = 0;

    if( queue )
    {
        EnterCriticalSection( &queue->cSection );

        /* empty the list of pending SendMessage waiting to be received */
        while (queue->smPending)
        {
            smsg = QUEUE_RemoveSMSG( queue, SM_PENDING_LIST, 0);

            senderQ = (MESSAGEQUEUE*)QUEUE_Lock( smsg->hSrcQueue );
            if ( !senderQ )
                continue;

            /* return 0, to unblock other thread */
            smsg->lResult = 0;
            smsg->flags |= SMSG_HAVE_RESULT;
            QUEUE_SetWakeBit( senderQ, QS_SMRESULT);
            
            QUEUE_Unlock( senderQ );
        }

        QUEUE_ClearWakeBit( queue, QS_SENDMESSAGE );
        
        LeaveCriticalSection( &queue->cSection );
    }
}


/***********************************************************************
 *	     QUEUE_DeleteMsgQueue
 *
 * Unlinks and deletes a message queue.
 *
 * Note: We need to mask asynchronous events to make sure PostMessage works
 * even in the signal handler.
 */
BOOL QUEUE_DeleteMsgQueue( HQUEUE16 hQueue )
{
    MESSAGEQUEUE * msgQueue = (MESSAGEQUEUE*)QUEUE_Lock(hQueue);
    HQUEUE16 *pPrev;

    TRACE_(msg)("(): Deleting message queue %04x\n", hQueue);

    if (!hQueue || !msgQueue)
    {
	ERR_(msg)("invalid argument.\n");
	return 0;
    }

    msgQueue->magic = 0;
    
    if( hCursorQueue == hQueue ) hCursorQueue = 0;
    if( hActiveQueue == hQueue ) hActiveQueue = 0;

    /* flush sent messages */
    QUEUE_FlushMessages( msgQueue );

    HeapLock( SystemHeap );  /* FIXME: a bit overkill */

    /* Release per queue data if present */
    if ( msgQueue->pQData )
    {
        PERQDATA_Release( msgQueue->pQData );
        msgQueue->pQData = 0;
    }
    
    /* remove the message queue from the global link list */
    pPrev = &hFirstQueue;
    while (*pPrev && (*pPrev != hQueue))
    {
        MESSAGEQUEUE *msgQ = (MESSAGEQUEUE*)GlobalLock16(*pPrev);

        /* sanity check */
        if ( !msgQ || (msgQ->magic != QUEUE_MAGIC) )
        {
            /* HQUEUE link list is corrupted, try to exit gracefully */
            ERR_(msg)("HQUEUE link list corrupted!\n");
            pPrev = 0;
            break;
        }
        pPrev = &msgQ->next;
    }
    if (pPrev && *pPrev) *pPrev = msgQueue->next;
    msgQueue->self = 0;

    HeapUnlock( SystemHeap );

    /* free up resource used by MESSAGEQUEUE structure */
    msgQueue->lockCount--;
    QUEUE_Unlock( msgQueue );
    
    return 1;
}


/***********************************************************************
 *           QUEUE_CreateSysMsgQueue
 *
 * Create the system message queue, and set the double-click speed.
 * Must be called only once.
 */
BOOL QUEUE_CreateSysMsgQueue( int size )
{
    /* Note: We dont need perQ data for the system message queue */
    if (!(hmemSysMsgQueue = QUEUE_CreateMsgQueue( FALSE )))
        return FALSE;
    
    sysMsgQueue = (MESSAGEQUEUE *) GlobalLock16( hmemSysMsgQueue );
    return TRUE;
}


/***********************************************************************
 *           QUEUE_GetSysQueue
 */
MESSAGEQUEUE *QUEUE_GetSysQueue(void)
{
    return sysMsgQueue;
}


/***********************************************************************
 *           QUEUE_SetWakeBit
 *
 * See "Windows Internals", p.449
 */
void QUEUE_SetWakeBit( MESSAGEQUEUE *queue, WORD bit )
{
    TRACE_(msg)("queue = %04x (wm=%04x), bit = %04x\n", 
	                queue->self, queue->wakeMask, bit );

    if (bit & QS_MOUSE) pMouseQueue = queue;
    if (bit & QS_KEY) pKbdQueue = queue;
    queue->changeBits |= bit;
    queue->wakeBits   |= bit;
    if (queue->wakeMask & bit)
    {
        queue->wakeMask = 0;

        /* Wake up thread waiting for message */
        if ( THREAD_IsWin16( queue->teb ) )
        {
            int iWndsLock = WIN_SuspendWndsLock();
            PostEvent16( queue->teb->htask16 );
            WIN_RestoreWndsLock( iWndsLock );
        }
        else
        {
            SERVER_START_REQ
            {
                struct wake_queue_request *req = server_alloc_req( sizeof(*req), 0 );
                req->handle = queue->server_queue;
                req->bits   = bit;
                server_call( REQ_WAKE_QUEUE );
            }
            SERVER_END_REQ;
        }
    }
}


/***********************************************************************
 *           QUEUE_ClearWakeBit
 */
void QUEUE_ClearWakeBit( MESSAGEQUEUE *queue, WORD bit )
{
    queue->changeBits &= ~bit;
    queue->wakeBits   &= ~bit;
}


/***********************************************************************
 *           QUEUE_WaitBits
 *
 * See "Windows Internals", p.447
 *
 * return values:
 *    0 if exit with timeout
 *    1 otherwise
 */
int QUEUE_WaitBits( WORD bits, DWORD timeout )
{
    MESSAGEQUEUE *queue;
    DWORD curTime = 0;
    HQUEUE16 hQueue;

    TRACE_(msg)("q %04x waiting for %04x\n", GetFastQueue16(), bits);

    if ( THREAD_IsWin16( NtCurrentTeb() ) && (timeout != INFINITE) )
        curTime = GetTickCount();

    hQueue = GetFastQueue16();
    if (!(queue = (MESSAGEQUEUE *)QUEUE_Lock( hQueue ))) return 0;
    
    for (;;)
    {
        if (queue->changeBits & bits)
        {
            /* One of the bits is set; we can return */
            queue->wakeMask = 0;
            QUEUE_Unlock( queue );
            return 1;
        }
        if (queue->wakeBits & QS_SENDMESSAGE)
        {
            /* Process the sent message immediately */

	    queue->wakeMask = 0;
            QUEUE_ReceiveMessage( queue );
	    continue;				/* nested sm crux */
        }

        queue->wakeMask = bits | QS_SENDMESSAGE;
        if(queue->changeBits & bits)
        {
            continue;
        }
	
	TRACE_(msg)("%04x) wakeMask is %04x, waiting\n", queue->self, queue->wakeMask);

        if ( !THREAD_IsWin16( NtCurrentTeb() ) )
        {
	    BOOL		bHasWin16Lock;
	    DWORD		dwlc;

	    if ( (bHasWin16Lock = _ConfirmWin16Lock()) )
	    {
	        TRACE_(msg)("bHasWin16Lock=TRUE\n");
	        ReleaseThunkLock( &dwlc );
	    }

	    WaitForSingleObject( queue->server_queue, timeout );

	    if ( bHasWin16Lock ) 
	    {
	        RestoreThunkLock( dwlc );
	    }
        }
        else
        {
            if ( timeout == INFINITE )
                WaitEvent16( 0 );  /* win 16 thread, use WaitEvent */
            else
            {
                /* check for timeout, then give control to other tasks */
                if (GetTickCount() - curTime > timeout)
                {

		    QUEUE_Unlock( queue );
                    return 0;   /* exit with timeout */
                }
                Yield16();
            }
	}
    }
}


/***********************************************************************
 *           QUEUE_AddSMSG
 *
 * This routine is called when a SMSG need to be added to one of the three
 * SM list.  (SM_PROCESSING_LIST, SM_PENDING_LIST, SM_WAITING_LIST)
 */
BOOL QUEUE_AddSMSG( MESSAGEQUEUE *queue, int list, SMSG *smsg )
{
    TRACE_(sendmsg)("queue=%x, list=%d, smsg=%p msg=%s\n", queue->self, list,
          smsg, SPY_GetMsgName(smsg->msg));
    
    switch (list)
    {
        case SM_PROCESSING_LIST:
            /* don't need to be thread safe, only accessed by the
             thread associated with the sender queue */
            smsg->nextProcessing = queue->smProcessing;
            queue->smProcessing = smsg;
            break;
            
        case SM_WAITING_LIST:
            /* don't need to be thread safe, only accessed by the
             thread associated with the receiver queue */
            smsg->nextWaiting = queue->smWaiting;
            queue->smWaiting = smsg;
            break;
            
        case SM_PENDING_LIST:
        {
            /* make it thread safe, could be accessed by the sender and
             receiver thread */
            SMSG **prev;

            EnterCriticalSection( &queue->cSection );
            smsg->nextPending = NULL;
            prev = &queue->smPending;
            while ( *prev )
                prev = &(*prev)->nextPending;
            *prev = smsg;
            LeaveCriticalSection( &queue->cSection );

            QUEUE_SetWakeBit( queue, QS_SENDMESSAGE );
            break;
        }

        default:
            ERR_(sendmsg)("Invalid list: %d", list);
            break;
    }

    return TRUE;
}


/***********************************************************************
 *           QUEUE_RemoveSMSG
 *
 * This routine is called when a SMSG needs to be removed from one of the three
 * SM lists (SM_PROCESSING_LIST, SM_PENDING_LIST, SM_WAITING_LIST).
 * If smsg == 0, remove the first smsg from the specified list
 */
SMSG *QUEUE_RemoveSMSG( MESSAGEQUEUE *queue, int list, SMSG *smsg )
{

    switch (list)
    {
        case SM_PROCESSING_LIST:
            /* don't need to be thread safe, only accessed by the
             thread associated with the sender queue */

            /* if smsg is equal to null, it means the first in the list */
            if (!smsg)
                smsg = queue->smProcessing;

            TRACE_(sendmsg)("queue=%x, list=%d, smsg=%p msg=%s\n", queue->self, list,
                  smsg, SPY_GetMsgName(smsg->msg));
            /* In fact SM_PROCESSING_LIST is a stack, and smsg
             should be always at the top of the list */
            if ( (smsg != queue->smProcessing) || !queue->smProcessing )
        {
                ERR_(sendmsg)("smsg not at the top of Processing list, smsg=0x%p queue=0x%p\n", smsg, queue);
                return 0;
            }
            else
            {
                queue->smProcessing = smsg->nextProcessing;
                smsg->nextProcessing = 0;
        }
            return smsg;

        case SM_WAITING_LIST:
            /* don't need to be thread safe, only accessed by the
             thread associated with the receiver queue */

            /* if smsg is equal to null, it means the first in the list */
            if (!smsg)
                smsg = queue->smWaiting;
            
            TRACE_(sendmsg)("queue=%x, list=%d, smsg=%p msg=%s\n", queue->self, list,
                  smsg, SPY_GetMsgName(smsg->msg));
            /* In fact SM_WAITING_LIST is a stack, and smsg
             should be always at the top of the list */
            if ( (smsg != queue->smWaiting) || !queue->smWaiting )
            {
                ERR_(sendmsg)("smsg not at the top of Waiting list, smsg=0x%p queue=0x%p\n", smsg, queue);
                return 0;
            }
            else
            {
                queue->smWaiting = smsg->nextWaiting;
                smsg->nextWaiting = 0;
    }
            return smsg;

        case SM_PENDING_LIST:
            /* make it thread safe, could be accessed by the sender and
             receiver thread */
            EnterCriticalSection( &queue->cSection );
    
            if (!smsg)
                smsg = queue->smPending;
	    if ( (smsg != queue->smPending) || !queue->smPending )
            {
                ERR_(sendmsg)("should always remove the top one in Pending list, smsg=0x%p queue=0x%p\n", smsg, queue);
		LeaveCriticalSection( &queue->cSection );
                return 0;
            }
            
            TRACE_(sendmsg)("queue=%x, list=%d, smsg=%p msg=%s\n", queue->self, list,
                  smsg, SPY_GetMsgName(smsg->msg));

            queue->smPending = smsg->nextPending;
            smsg->nextPending = 0;

            /* if no more SMSG in Pending list, clear QS_SENDMESSAGE flag */
            if (!queue->smPending)
                QUEUE_ClearWakeBit( queue, QS_SENDMESSAGE );
            
            LeaveCriticalSection( &queue->cSection );
            return smsg;

        default:
            ERR_(sendmsg)("Invalid list: %d\n", list);
            break;
    }

    return 0;
}


/***********************************************************************
 *           QUEUE_ReceiveMessage
 * 
 * This routine is called when a sent message is waiting for the queue.
 */
void QUEUE_ReceiveMessage( MESSAGEQUEUE *queue )
{
    LRESULT       result = 0;
    SMSG          *smsg;
    MESSAGEQUEUE  *senderQ;

    TRACE_(sendmsg)("queue %04x\n", queue->self );

    if ( !((queue->wakeBits & QS_SENDMESSAGE) && queue->smPending) )
    {
        TRACE_(sendmsg)("\trcm: nothing to do\n");
        return;
    }

    /* remove smsg on the top of the pending list and put it in the processing list */
    smsg = QUEUE_RemoveSMSG(queue, SM_PENDING_LIST, 0);
    QUEUE_AddSMSG(queue, SM_WAITING_LIST, smsg);

    TRACE_(sendmsg)("RM: %s [%04x] (%04x -> %04x)\n",
       	    SPY_GetMsgName(smsg->msg), smsg->msg, smsg->hSrcQueue, smsg->hDstQueue );

    if (IsWindow( smsg->hWnd ))
    {
        WND *wndPtr = WIN_FindWndPtr( smsg->hWnd );
        DWORD extraInfo = queue->GetMessageExtraInfoVal; /* save ExtraInfo */

        /* use sender queue extra info value while calling the window proc */
        senderQ = (MESSAGEQUEUE*)QUEUE_Lock( smsg->hSrcQueue );
        if (senderQ)
  {
            queue->GetMessageExtraInfoVal = senderQ->GetMessageExtraInfoVal;
            QUEUE_Unlock( senderQ );
        }

        /* call the right version of CallWindowProcXX */
        if (smsg->flags & SMSG_WIN32)
        {
            TRACE_(sendmsg)("\trcm: msg is Win32\n" );
            if (smsg->flags & SMSG_UNICODE)
                result = CallWindowProcW( wndPtr->winproc,
                                            smsg->hWnd, smsg->msg,
                                            smsg->wParam, smsg->lParam );
            else
                result = CallWindowProcA( wndPtr->winproc,
                                            smsg->hWnd, smsg->msg,
                                            smsg->wParam, smsg->lParam );
        }
        else  /* Win16 message */
            result = CallWindowProc16( (WNDPROC16)wndPtr->winproc,
                                       (HWND16) smsg->hWnd,
                                       (UINT16) smsg->msg,
                                       LOWORD (smsg->wParam),
                                       smsg->lParam );

        queue->GetMessageExtraInfoVal = extraInfo;  /* Restore extra info */
        WIN_ReleaseWndPtr(wndPtr);
	TRACE_(sendmsg)("result =  %08x\n", (unsigned)result );
    }
    else WARN_(sendmsg)("\trcm: bad hWnd\n");

    
        /* set SMSG_SENDING_REPLY flag to tell ReplyMessage16, it's not
         an early reply */
        smsg->flags |= SMSG_SENDING_REPLY;
        ReplyMessage( result );

    TRACE_(sendmsg)("done!\n" );
}



/***********************************************************************
 *           QUEUE_AddMsg
 *
 * Add a message to the queue. Return FALSE if queue is full.
 */
BOOL QUEUE_AddMsg( HQUEUE16 hQueue, int type, MSG *msg, DWORD extraInfo )
{
    MESSAGEQUEUE *msgQueue;
    QMSG         *qmsg;


    if (!(msgQueue = (MESSAGEQUEUE *)QUEUE_Lock( hQueue ))) return FALSE;

    /* allocate new message in global heap for now */
    if (!(qmsg = (QMSG *) HeapAlloc( SystemHeap, 0, sizeof(QMSG) ) ))
    {
        QUEUE_Unlock( msgQueue );
        return 0;
    }

    EnterCriticalSection( &msgQueue->cSection );

      /* Store message */
    qmsg->type = type;
    qmsg->msg = *msg;
    qmsg->extraInfo = extraInfo;

    /* insert the message in the link list */
    qmsg->nextMsg = 0;
    qmsg->prevMsg = msgQueue->lastMsg;

    if (msgQueue->lastMsg)
        msgQueue->lastMsg->nextMsg = qmsg;

    /* update first and last anchor in message queue */
    msgQueue->lastMsg = qmsg;
    if (!msgQueue->firstMsg)
        msgQueue->firstMsg = qmsg;
    
    msgQueue->msgCount++;

    LeaveCriticalSection( &msgQueue->cSection );

    QUEUE_SetWakeBit( msgQueue, QS_POSTMESSAGE );
    QUEUE_Unlock( msgQueue );
    
    return TRUE;
}



/***********************************************************************
 *           QUEUE_FindMsg
 *
 * Find a message matching the given parameters. Return -1 if none available.
 */
QMSG* QUEUE_FindMsg( MESSAGEQUEUE * msgQueue, HWND hwnd, int first, int last )
{
    QMSG* qmsg;

    EnterCriticalSection( &msgQueue->cSection );

    if (!msgQueue->msgCount)
        qmsg = 0;
    else if (!hwnd && !first && !last)
        qmsg = msgQueue->firstMsg;
    else
    {
        /* look in linked list for message matching first and last criteria */
        for (qmsg = msgQueue->firstMsg; qmsg; qmsg = qmsg->nextMsg)
    {
            MSG *msg = &(qmsg->msg);

	if (!hwnd || (msg->hwnd == hwnd))
	{
                if (!first && !last)
                    break;   /* found it */
                
                if ((msg->message >= first) && (!last || (msg->message <= last)))
                    break;   /* found it */
            }
	}
    }
    
    LeaveCriticalSection( &msgQueue->cSection );

    return qmsg;
}



/***********************************************************************
 *           QUEUE_RemoveMsg
 *
 * Remove a message from the queue (pos must be a valid position).
 */
void QUEUE_RemoveMsg( MESSAGEQUEUE * msgQueue, QMSG *qmsg )
{
    EnterCriticalSection( &msgQueue->cSection );

    /* set the linked list */
    if (qmsg->prevMsg)
        qmsg->prevMsg->nextMsg = qmsg->nextMsg;

    if (qmsg->nextMsg)
        qmsg->nextMsg->prevMsg = qmsg->prevMsg;

    if (msgQueue->firstMsg == qmsg)
        msgQueue->firstMsg = qmsg->nextMsg;

    if (msgQueue->lastMsg == qmsg)
        msgQueue->lastMsg = qmsg->prevMsg;

    /* deallocate the memory for the message */
    HeapFree( SystemHeap, 0, qmsg );
    
    msgQueue->msgCount--;
    if (!msgQueue->msgCount) msgQueue->wakeBits &= ~QS_POSTMESSAGE;

    LeaveCriticalSection( &msgQueue->cSection );
}


/***********************************************************************
 *           QUEUE_WakeSomeone
 *
 * Wake a queue upon reception of a hardware event.
 */
static void QUEUE_WakeSomeone( UINT message )
{
    WND*	  wndPtr = NULL;
    WORD          wakeBit;
    HWND hwnd;
    HQUEUE16     hQueue = 0;
    MESSAGEQUEUE *queue = NULL;

    if (hCursorQueue)
        hQueue = hCursorQueue;

    if( (message >= WM_KEYFIRST) && (message <= WM_KEYLAST) )
    {
       wakeBit = QS_KEY;
       if( hActiveQueue )
           hQueue = hActiveQueue;
    }
    else 
    {
       wakeBit = (message == WM_MOUSEMOVE) ? QS_MOUSEMOVE : QS_MOUSEBUTTON;
       if( (hwnd = GetCapture()) )
	 if( (wndPtr = WIN_FindWndPtr( hwnd )) ) 
           {
               hQueue = wndPtr->hmemTaskQ;
               WIN_ReleaseWndPtr(wndPtr);
           }
    }

    if( (hwnd = GetSysModalWindow16()) )
    {
      if( (wndPtr = WIN_FindWndPtr( hwnd )) )
        {
            hQueue = wndPtr->hmemTaskQ;
            WIN_ReleaseWndPtr(wndPtr);
        }
    }

    if (hQueue)
        queue = QUEUE_Lock( hQueue );
    
    if( !queue ) 
    {
        queue = QUEUE_Lock( hFirstQueue );
      while( queue )
      {
        if (queue->wakeMask & wakeBit) break;
          
            QUEUE_Unlock(queue);
            queue = QUEUE_Lock( queue->next );
      }
      if( !queue )
      { 
        WARN_(msg)("couldn't find queue\n"); 
        return; 
      }
    }

    QUEUE_SetWakeBit( queue, wakeBit );

    QUEUE_Unlock( queue );
}


/***********************************************************************
 *           hardware_event
 *
 * Add an event to the system message queue.
 * Note: the position is relative to the desktop window.
 */
void hardware_event( UINT message, WPARAM wParam, LPARAM lParam,
		     int xPos, int yPos, DWORD time, DWORD extraInfo )
{
    MSG *msg;
    QMSG  *qmsg;
    int  mergeMsg = 0;

    if (!sysMsgQueue) return;

    EnterCriticalSection( &sysMsgQueue->cSection );

    /* Merge with previous event if possible */
    qmsg = sysMsgQueue->lastMsg;

    if ((message == WM_MOUSEMOVE) && sysMsgQueue->lastMsg)
    {
        msg = &(sysMsgQueue->lastMsg->msg);
        
	if ((msg->message == message) && (msg->wParam == wParam))
        {
            /* Merge events */
            qmsg = sysMsgQueue->lastMsg;
            mergeMsg = 1;
    }
    }

    if (!mergeMsg)
    {
        /* Should I limit the number of messages in
          the system message queue??? */

        /* Don't merge allocate a new msg in the global heap */
        
        if (!(qmsg = (QMSG *) HeapAlloc( SystemHeap, 0, sizeof(QMSG) ) ))
        {
            LeaveCriticalSection( &sysMsgQueue->cSection );
            return;
        }
        
        /* put message at the end of the linked list */
        qmsg->nextMsg = 0;
        qmsg->prevMsg = sysMsgQueue->lastMsg;

        if (sysMsgQueue->lastMsg)
            sysMsgQueue->lastMsg->nextMsg = qmsg;

        /* set last and first anchor index in system message queue */
        sysMsgQueue->lastMsg = qmsg;
        if (!sysMsgQueue->firstMsg)
            sysMsgQueue->firstMsg = qmsg;
        
        sysMsgQueue->msgCount++;
    }

      /* Store message */
    msg = &(qmsg->msg);
    msg->hwnd    = 0;
    msg->message = message;
    msg->wParam  = wParam;
    msg->lParam  = lParam;
    msg->time    = time;
    msg->pt.x    = xPos;
    msg->pt.y    = yPos;
    qmsg->extraInfo = extraInfo;
    qmsg->type      = QMSG_HARDWARE;

    LeaveCriticalSection( &sysMsgQueue->cSection );

    QUEUE_WakeSomeone( message );
}

		    
/***********************************************************************
 *	     QUEUE_GetQueueTask
 */
HTASK16 QUEUE_GetQueueTask( HQUEUE16 hQueue )
{
    HTASK16 hTask = 0;
    
    MESSAGEQUEUE *queue = QUEUE_Lock( hQueue );

    if (queue)
    {
        hTask = queue->teb->htask16;
        QUEUE_Unlock( queue );
    }

    return hTask;
}



/***********************************************************************
 *           QUEUE_IncPaintCount
 */
void QUEUE_IncPaintCount( HQUEUE16 hQueue )
{
    MESSAGEQUEUE *queue;

    if (!(queue = (MESSAGEQUEUE *)QUEUE_Lock( hQueue ))) return;
    queue->wPaintCount++;
    QUEUE_SetWakeBit( queue, QS_PAINT );
    QUEUE_Unlock( queue );
}


/***********************************************************************
 *           QUEUE_DecPaintCount
 */
void QUEUE_DecPaintCount( HQUEUE16 hQueue )
{
    MESSAGEQUEUE *queue;

    if (!(queue = (MESSAGEQUEUE *)QUEUE_Lock( hQueue ))) return;
    queue->wPaintCount--;
    if (!queue->wPaintCount) queue->wakeBits &= ~QS_PAINT;
    QUEUE_Unlock( queue );
}


/***********************************************************************
 *           QUEUE_IncTimerCount
 */
void QUEUE_IncTimerCount( HQUEUE16 hQueue )
{
    MESSAGEQUEUE *queue;

    if (!(queue = (MESSAGEQUEUE *)QUEUE_Lock( hQueue ))) return;
    queue->wTimerCount++;
    QUEUE_SetWakeBit( queue, QS_TIMER );
    QUEUE_Unlock( queue );
}


/***********************************************************************
 *           QUEUE_DecTimerCount
 */
void QUEUE_DecTimerCount( HQUEUE16 hQueue )
{
    MESSAGEQUEUE *queue;

    if (!(queue = (MESSAGEQUEUE *)QUEUE_Lock( hQueue ))) return;
    queue->wTimerCount--;
    if (!queue->wTimerCount) queue->wakeBits &= ~QS_TIMER;
    QUEUE_Unlock( queue );
}


/***********************************************************************
 *           PostQuitMessage16   (USER.6)
 */
void WINAPI PostQuitMessage16( INT16 exitCode )
{
    PostQuitMessage( exitCode );
}


/***********************************************************************
 *           PostQuitMessage   (USER32.421)
 *
 * PostQuitMessage() posts a message to the system requesting an
 * application to terminate execution. As a result of this function,
 * the WM_QUIT message is posted to the application, and
 * PostQuitMessage() returns immediately.  The exitCode parameter
 * specifies an application-defined exit code, which appears in the
 * _wParam_ parameter of the WM_QUIT message posted to the application.  
 *
 * CONFORMANCE
 *
 *  ECMA-234, Win32
 */
void WINAPI PostQuitMessage( INT exitCode )
{
    MESSAGEQUEUE *queue;

    if (!(queue = (MESSAGEQUEUE *)QUEUE_Lock( GetFastQueue16() ))) return;
    queue->wPostQMsg = TRUE;
    queue->wExitCode = (WORD)exitCode;
    QUEUE_Unlock( queue );
}


/***********************************************************************
 *           GetWindowTask16   (USER.224)
 */
HTASK16 WINAPI GetWindowTask16( HWND16 hwnd )
{
    HTASK16 retvalue;
    WND *wndPtr = WIN_FindWndPtr( hwnd );

    if (!wndPtr) return 0;
    retvalue = QUEUE_GetQueueTask( wndPtr->hmemTaskQ );
    WIN_ReleaseWndPtr(wndPtr);
    return retvalue;
}

/***********************************************************************
 *           GetWindowThreadProcessId   (USER32.313)
 */
DWORD WINAPI GetWindowThreadProcessId( HWND hwnd, LPDWORD process )
{
    DWORD retvalue;
    MESSAGEQUEUE *queue;

    WND *wndPtr = WIN_FindWndPtr( hwnd );
    if (!wndPtr) return 0;

    queue = QUEUE_Lock( wndPtr->hmemTaskQ );
    WIN_ReleaseWndPtr(wndPtr);

    if (!queue) return 0;

    if ( process ) *process = (DWORD)queue->teb->pid;
    retvalue = (DWORD)queue->teb->tid;

    QUEUE_Unlock( queue );
    return retvalue;
}


/***********************************************************************
 *           SetMessageQueue16   (USER.266)
 */
BOOL16 WINAPI SetMessageQueue16( INT16 size )
{
    return SetMessageQueue( size );
}


/***********************************************************************
 *           SetMessageQueue   (USER32.494)
 */
BOOL WINAPI SetMessageQueue( INT size )
{
    /* now obsolete the message queue will be expanded dynamically
     as necessary */

    /* access the queue to create it if it's not existing */
    GetFastQueue16();

    return TRUE;
}

/***********************************************************************
 *           InitThreadInput16   (USER.409)
 */
HQUEUE16 WINAPI InitThreadInput16( WORD unknown, WORD flags )
{
    HQUEUE16 hQueue;
    MESSAGEQUEUE *queuePtr;

    TEB *teb = NtCurrentTeb();

    if (!teb)
        return 0;

    hQueue = teb->queue;
    
    if ( !hQueue )
    {
        /* Create thread message queue */
        if( !(hQueue = QUEUE_CreateMsgQueue( TRUE )))
        {
            ERR_(msg)("failed!\n");
            return FALSE;
	}
        
        /* Link new queue into list */
        queuePtr = (MESSAGEQUEUE *)QUEUE_Lock( hQueue );
        queuePtr->teb = NtCurrentTeb();

        HeapLock( SystemHeap );  /* FIXME: a bit overkill */
        SetThreadQueue16( 0, hQueue );
        teb->queue = hQueue;
            
        queuePtr->next  = hFirstQueue;
        hFirstQueue = hQueue;
        HeapUnlock( SystemHeap );
        
        QUEUE_Unlock( queuePtr );
    }

    return hQueue;
}

/***********************************************************************
 *           GetQueueStatus16   (USER.334)
 */
DWORD WINAPI GetQueueStatus16( UINT16 flags )
{
    MESSAGEQUEUE *queue;
    DWORD ret;

    if (!(queue = (MESSAGEQUEUE *)QUEUE_Lock( GetFastQueue16() ))) return 0;
    ret = MAKELONG( queue->changeBits, queue->wakeBits );
    queue->changeBits = 0;
    QUEUE_Unlock( queue );
    
    return ret & MAKELONG( flags, flags );
}

/***********************************************************************
 *           GetQueueStatus   (USER32.283)
 */
DWORD WINAPI GetQueueStatus( UINT flags )
{
    MESSAGEQUEUE *queue;
    DWORD ret;

    if (!(queue = (MESSAGEQUEUE *)QUEUE_Lock( GetFastQueue16() ))) return 0;
    ret = MAKELONG( queue->changeBits, queue->wakeBits );
    queue->changeBits = 0;
    QUEUE_Unlock( queue );
    
    return ret & MAKELONG( flags, flags );
}


/***********************************************************************
 *           GetInputState16   (USER.335)
 */
BOOL16 WINAPI GetInputState16(void)
{
    return GetInputState();
}

/***********************************************************************
 *           WaitForInputIdle   (USER32.577)
 */
DWORD WINAPI WaitForInputIdle (HANDLE hProcess, DWORD dwTimeOut)
{
    DWORD cur_time, ret;
    HANDLE idle_event = -1;

    SERVER_START_REQ
    {
        struct wait_input_idle_request *req = server_alloc_req( sizeof(*req), 0 );
        req->handle = hProcess;
        req->timeout = dwTimeOut;
        if (!(ret = server_call( REQ_WAIT_INPUT_IDLE ))) idle_event = req->event;
    }
    SERVER_END_REQ;
    if (ret) return 0xffffffff;  /* error */
    if (idle_event == -1) return 0;  /* no event to wait on */

  cur_time = GetTickCount();

  TRACE_(msg)("waiting for %x\n", idle_event );
  while ( dwTimeOut > GetTickCount() - cur_time || dwTimeOut == INFINITE ) {

    ret = MsgWaitForMultipleObjects ( 1, &idle_event, FALSE, dwTimeOut, QS_SENDMESSAGE );
    if ( ret == ( WAIT_OBJECT_0 + 1 )) {
      MESSAGEQUEUE * queue;
      if (!(queue = (MESSAGEQUEUE *)QUEUE_Lock( GetFastQueue16() ))) return 0xFFFFFFFF;
      QUEUE_ReceiveMessage ( queue );
      QUEUE_Unlock ( queue );
      continue; 
    }
    if ( ret == WAIT_TIMEOUT || ret == 0xFFFFFFFF ) {
      TRACE_(msg)("timeout or error\n");
      return ret;
    }
    else {
      TRACE_(msg)("finished\n");
      return 0;
    }
    
  }
  return WAIT_TIMEOUT;
}

/***********************************************************************
 *           GetInputState   (USER32.244)
 */
BOOL WINAPI GetInputState(void)
{
    MESSAGEQUEUE *queue;
    BOOL ret;

    if (!(queue = (MESSAGEQUEUE *)QUEUE_Lock( GetFastQueue16() )))
        return FALSE;
    ret = queue->wakeBits & (QS_KEY | QS_MOUSEBUTTON);
    QUEUE_Unlock( queue );

    return ret;
}

/***********************************************************************
 *           UserYield  (USER.332)
 */
void WINAPI UserYield16(void)
{
    MESSAGEQUEUE *queue;

    /* Handle sent messages */
    queue = (MESSAGEQUEUE *)QUEUE_Lock( GetFastQueue16() );

    while (queue && (queue->wakeBits & QS_SENDMESSAGE))
        QUEUE_ReceiveMessage( queue );

    QUEUE_Unlock( queue );
    
    /* Yield */
    if ( THREAD_IsWin16( NtCurrentTeb() ) )
        OldYield16();
    else
        WIN32_OldYield16();

    /* Handle sent messages again */
    queue = (MESSAGEQUEUE *)QUEUE_Lock( GetFastQueue16() );

    while (queue && (queue->wakeBits & QS_SENDMESSAGE))
        QUEUE_ReceiveMessage( queue );

    QUEUE_Unlock( queue );
}

/***********************************************************************
 *           GetMessagePos   (USER.119) (USER32.272)
 * 
 * The GetMessagePos() function returns a long value representing a
 * cursor position, in screen coordinates, when the last message
 * retrieved by the GetMessage() function occurs. The x-coordinate is
 * in the low-order word of the return value, the y-coordinate is in
 * the high-order word. The application can use the MAKEPOINT()
 * macro to obtain a POINT structure from the return value. 
 *
 * For the current cursor position, use GetCursorPos().
 *
 * RETURNS
 *
 * Cursor position of last message on success, zero on failure.
 *
 * CONFORMANCE
 *
 * ECMA-234, Win32
 *
 */
DWORD WINAPI GetMessagePos(void)
{
    MESSAGEQUEUE *queue;
    DWORD ret;

    if (!(queue = (MESSAGEQUEUE *)QUEUE_Lock( GetFastQueue16() ))) return 0;
    ret = queue->GetMessagePosVal;
    QUEUE_Unlock( queue );

    return ret;
}


/***********************************************************************
 *           GetMessageTime   (USER.120) (USER32.273)
 *
 * GetMessageTime() returns the message time for the last message
 * retrieved by the function. The time is measured in milliseconds with
 * the same offset as GetTickCount().
 *
 * Since the tick count wraps, this is only useful for moderately short
 * relative time comparisons.
 *
 * RETURNS
 *
 * Time of last message on success, zero on failure.
 *
 * CONFORMANCE
 *
 * ECMA-234, Win32
 *  
 */
LONG WINAPI GetMessageTime(void)
{
    MESSAGEQUEUE *queue;
    LONG ret;

    if (!(queue = (MESSAGEQUEUE *)QUEUE_Lock( GetFastQueue16() ))) return 0;
    ret = queue->GetMessageTimeVal;
    QUEUE_Unlock( queue );
    
    return ret;
}


/***********************************************************************
 *           GetMessageExtraInfo   (USER.288) (USER32.271)
 */
LONG WINAPI GetMessageExtraInfo(void)
{
    MESSAGEQUEUE *queue;
    LONG ret;

    if (!(queue = (MESSAGEQUEUE *)QUEUE_Lock( GetFastQueue16() ))) return 0;
    ret = queue->GetMessageExtraInfoVal;
    QUEUE_Unlock( queue );

    return ret;
}


/**********************************************************************
 * AttachThreadInput [USER32.8]  Attaches input of 1 thread to other
 *
 * Attaches the input processing mechanism of one thread to that of
 * another thread.
 *
 * RETURNS
 *    Success: TRUE
 *    Failure: FALSE
 *
 * TODO:
 *    1. Reset the Key State (currenly per thread key state is not maintained)
 */
BOOL WINAPI AttachThreadInput( 
    DWORD idAttach,   /* [in] Thread to attach */
    DWORD idAttachTo, /* [in] Thread to attach to */
    BOOL fAttach)   /* [in] Attach or detach */
{
    MESSAGEQUEUE *pSrcMsgQ = 0, *pTgtMsgQ = 0;
    BOOL16 bRet = 0;

    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);

    /* A thread cannot attach to itself */
    if ( idAttach == idAttachTo )
        goto CLEANUP;

    /* According to the docs this method should fail if a
     * "Journal record" hook is installed. (attaches all input queues together)
     */
    if ( HOOK_IsHooked( WH_JOURNALRECORD ) )
        goto CLEANUP;
        
    /* Retrieve message queues corresponding to the thread id's */
    pTgtMsgQ = (MESSAGEQUEUE *)QUEUE_Lock( GetThreadQueue16( idAttach ) );
    pSrcMsgQ = (MESSAGEQUEUE *)QUEUE_Lock( GetThreadQueue16( idAttachTo ) );

    /* Ensure we have message queues and that Src and Tgt threads
     * are not system threads.
     */
    if ( !pSrcMsgQ || !pTgtMsgQ || !pSrcMsgQ->pQData || !pTgtMsgQ->pQData )
        goto CLEANUP;

    if (fAttach)   /* Attach threads */
    {
        /* Only attach if currently detached  */
        if ( pTgtMsgQ->pQData != pSrcMsgQ->pQData )
        {
            /* First release the target threads perQData */
            PERQDATA_Release( pTgtMsgQ->pQData );
        
            /* Share a reference to the source threads perQDATA */
            PERQDATA_Addref( pSrcMsgQ->pQData );
            pTgtMsgQ->pQData = pSrcMsgQ->pQData;
        }
    }
    else    /* Detach threads */
    {
        /* Only detach if currently attached */
        if ( pTgtMsgQ->pQData == pSrcMsgQ->pQData )
        {
            /* First release the target threads perQData */
            PERQDATA_Release( pTgtMsgQ->pQData );
        
            /* Give the target thread its own private perQDATA once more */
            pTgtMsgQ->pQData = PERQDATA_CreateInstance();
        }
    }

    /* TODO: Reset the Key State */

    bRet = 1;      /* Success */
    
CLEANUP:

    /* Unlock the queues before returning */
    if ( pSrcMsgQ )
        QUEUE_Unlock( pSrcMsgQ );
    if ( pTgtMsgQ )
        QUEUE_Unlock( pTgtMsgQ );
    
    return bRet;
}
