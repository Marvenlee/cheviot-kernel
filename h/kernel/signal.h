#ifndef KERNEL_SIGNAL_H
#define KERNEL_SIGNAL_H

#include <kernel/lists.h>
#include <kernel/types.h>
#include <signal.h>
//#include <ucontext.h>

// Forward Declarations
struct Process;


/*
 * Bitmap masks of some signal properties
 */

#define SIGBIT(sig)	(1UL<<(sig-1)) 


#define SIGCANTMASK (SIGBIT(SIGKILL) | SIGBIT(SIGSTOP))
#define STOPSIGMASK (SIGBIT(SIGSTOP) | SIGBIT(SIGTSTP) | SIGBIT(SIGTTIN) | SIGBIT(SIGTTOU))
#define CONTSIGMASK (SIGBIT(SIGFCONT))
#define SYNCSIGMASK (SIGBIT(SIGILL) | SIGBIT(SIGTRAP) | SIGBIT(SIGBUS) | SIGBIT(SIGFPE) | SIGBIT(SIGSEGV))




/*
 * Flags used in the sigprop[] array
 */
 
#define SP_KILL     (1<<0)
#define SP_CORE     (1<<1)
#define SP_NORESET  (1<<2)
#define SP_CANTMASK (1<<3)
#define SP_CONT     (1<<4)
#define SP_STOP     (1<<5)
#define SP_TTYSTOP  (1<<6)


/*
 * User/nix signal state of a Process.
 */

struct ProcSignalState {
  _sig_func_ptr handler[NSIG]; /* Action for each signal, func, SIG_DFL, SIG_IGN */
  sigset_t handler_mask[NSIG]; /* Mask to add to current sig_mask during handler.*/
 
  void (*restorer)(void); /* User-mode signal trampoline, takes care of calling sys_sigreturn */ 
  struct sigframe *sigreturn_sigframe;

  sigset_t sig_info; /* bitmap of signals that want SA_SIGINFO args. */
  sigset_t sig_resethand; /* Unreliable signals */
  sigset_t sig_nodefer;   /* Don't automatically mask delivered signal */
  sigset_t sigsuspend_oldmask;
  bool use_sigsuspend_mask;

  sigset_t sig_mask;        /* Current signal mask */
  sigset_t sig_pending;     /* bitmap of signals awaiting delivery */
  int8_t si_code[NSIG];     /* Async pending signal code (user, message etc) */ 
};


struct ThreadSignalState {
  sigset_t sig_mask;    /* Current signal mask */
  sigset_t sig_pending; /* bitmap of signals awaiting delivery */

//  void *sigsegv_ptr;  // Stored upon exception, cleared on delivery
//  void *sigill_ptr;   // Stored upon exception, cleared on delivery
};


/*
 * Variables
 */

extern const uint32_t sigprop[NSIG];

/*
 * Prototypes
 */


int sys_kill(int pid, int signal);
int sys_sigaction(int how, const struct sigaction *act, struct sigaction *oact);
int sys_sigsuspend(const sigset_t *mask);
int sys_sigprocmask(int how, const sigset_t *set, sigset_t *oset);
int sys_sigpending(sigset_t *set);
void sig_exit(int signal);
void init_signals(struct Process *dst);
void fork_signals(struct Process *dst, struct Process *src);
void exec_signals(struct Process *dst);
void do_signal_default(int sig);
int do_kill_process(pid_t pid, int signal);
int do_kill_process_group(pid_t pid, int signal);
int pick_signal(uint32_t sigbits);

int do_signal_process(struct Process *proc, int signal);
int do_signal_thread(struct Thread *thread, int signal);

#endif
