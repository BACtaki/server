/*****************************************************************************

Copyright (c) 1996, 2016, Oracle and/or its affiliates. All Rights Reserved.
Copyright (c) 2017, 2018, MariaDB Corporation.

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation; version 2 of the License.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Suite 500, Boston, MA 02110-1335 USA

*****************************************************************************/

/**************************************************//**
@file include/trx0purge.h
Purge old versions

Created 3/26/1996 Heikki Tuuri
*******************************************************/

#ifndef trx0purge_h
#define trx0purge_h

#include "trx0rseg.h"
#include "que0types.h"

/** A dummy undo record used as a return value when we have a whole undo log
which needs no purge */
extern trx_undo_rec_t	trx_purge_dummy_rec;

/********************************************************************//**
Calculates the file address of an undo log header when we have the file
address of its history list node.
@return file address of the log */
UNIV_INLINE
fil_addr_t
trx_purge_get_log_from_hist(
/*========================*/
	fil_addr_t	node_addr);	/*!< in: file address of the history
					list node of the log */
/** Prepend the history list with an undo log.
Remove the undo log segment from the rseg slot if it is too big for reuse.
@param[in]	trx		transaction
@param[in,out]	undo		undo log
@param[in,out]	mtr		mini-transaction */
void
trx_purge_add_undo_to_history(const trx_t* trx, trx_undo_t*& undo, mtr_t* mtr);
/*******************************************************************//**
This function runs a purge batch.
@return number of undo log pages handled in the batch */
ulint
trx_purge(
/*======*/
	ulint	n_purge_threads,	/*!< in: number of purge tasks to
					submit to task queue. */
	bool	truncate);		/*!< in: truncate history if true */
/*******************************************************************//**
Stop purge and wait for it to stop, move to PURGE_STATE_STOP. */
void
trx_purge_stop(void);
/*================*/
/*******************************************************************//**
Resume purge, move to PURGE_STATE_RUN. */
void
trx_purge_run(void);
/*================*/

/** Purge states */
enum purge_state_t {
	PURGE_STATE_INIT,		/*!< Purge instance created */
	PURGE_STATE_RUN,		/*!< Purge should be running */
	PURGE_STATE_STOP,		/*!< Purge should be stopped */
	PURGE_STATE_EXIT,		/*!< Purge has been shutdown */
	PURGE_STATE_DISABLED		/*!< Purge was never started */
};

/*******************************************************************//**
Get the purge state.
@return purge state. */
purge_state_t
trx_purge_state(void);
/*=================*/

/** Rollback segements from a given transaction with trx-no
scheduled for purge. */
class TrxUndoRsegs {
private:
	typedef std::vector<trx_rseg_t*, ut_allocator<trx_rseg_t*> >
		trx_rsegs_t;
public:
	typedef trx_rsegs_t::iterator iterator;
	typedef trx_rsegs_t::const_iterator const_iterator;

	/** Default constructor */
	TrxUndoRsegs() {}
	/** Constructor */
	TrxUndoRsegs(trx_rseg_t& rseg)
		: m_commit(rseg.last_commit), m_rsegs(1, &rseg) {}
	/** Constructor */
	TrxUndoRsegs(trx_id_t trx_no, trx_rseg_t& rseg)
		: m_commit(trx_no << 1), m_rsegs(1, &rseg) {}

	/** @return the transaction commit identifier */
	trx_id_t trx_no() const { return m_commit >> 1; }

	bool operator!=(const TrxUndoRsegs& other) const
	{ return m_commit != other.m_commit; }
	bool empty() const { return m_rsegs.empty(); }
	void erase(iterator& it) { m_rsegs.erase(it); }
	iterator begin() { return(m_rsegs.begin()); }
	iterator end() { return(m_rsegs.end()); }
	const_iterator begin() const { return m_rsegs.begin(); }
	const_iterator end() const { return m_rsegs.end(); }

	/** Compare two TrxUndoRsegs based on trx_no.
	@param elem1 first element to compare
	@param elem2 second element to compare
	@return true if elem1 > elem2 else false.*/
	bool operator()(const TrxUndoRsegs& lhs, const TrxUndoRsegs& rhs)
	{
		return(lhs.m_commit > rhs.m_commit);
	}

private:
	/** Copy trx_rseg_t::last_commit */
	trx_id_t		m_commit;
	/** Rollback segments of a transaction, scheduled for purge. */
	trx_rsegs_t		m_rsegs;
};

typedef std::priority_queue<
	TrxUndoRsegs,
	std::vector<TrxUndoRsegs, ut_allocator<TrxUndoRsegs> >,
	TrxUndoRsegs>	purge_pq_t;

/** Chooses the rollback segment with the oldest committed transaction */
struct TrxUndoRsegsIterator {
	/** Constructor */
	TrxUndoRsegsIterator();
	/** Sets the next rseg to purge in purge_sys.
	Executed in the purge coordinator thread.
	@return whether anything is to be purged */
	inline bool set_next();

private:
	// Disable copying
	TrxUndoRsegsIterator(const TrxUndoRsegsIterator&);
	TrxUndoRsegsIterator& operator=(const TrxUndoRsegsIterator&);

	/** The current element to process */
	TrxUndoRsegs			m_rsegs;
	/** Track the current element in m_rsegs */
	TrxUndoRsegs::const_iterator	m_iter;
};

/* Namespace to hold all the related functions and variables need for truncate
of undo tablespace. */
namespace undo {

	typedef std::vector<ulint>		undo_spaces_t;
	typedef	std::vector<trx_rseg_t*>	rseg_for_trunc_t;

	/** Magic Number to indicate truncate action is complete. */
	const ib_uint32_t			s_magic = 76845412;

	/** Truncate Log file Prefix. */
	const char* const			s_log_prefix = "undo_";

	/** Truncate Log file Extension. */
	const char* const			s_log_ext = "trunc.log";

	/** Populate log file name based on space_id
	@param[in]	space_id	id of the undo tablespace.
	@return DB_SUCCESS or error code */
	dberr_t populate_log_file_name(
		ulint	space_id,
		char*&	log_file_name);

	/** Create the truncate log file.
	@param[in]	space_id	id of the undo tablespace to truncate.
	@return DB_SUCCESS or error code. */
	dberr_t init(ulint space_id);

	/** Mark completion of undo truncate action by writing magic number to
	the log file and then removing it from the disk.
	If we are going to remove it from disk then why write magic number ?
	This is to safeguard from unlink (file-system) anomalies that will keep
	the link to the file even after unlink action is successfull and
	ref-count = 0.
	@param[in]	space_id	id of the undo tablespace to truncate.*/
	void done(ulint	space_id);

	/** Check if TRUNCATE_DDL_LOG file exist.
	@param[in]	space_id	id of the undo tablespace.
	@return true if exist else false. */
	bool is_log_present(ulint space_id);

	/** Track UNDO tablespace mark for truncate. */
	class Truncate {
	public:
		void create()
		{
			m_undo_for_trunc = ULINT_UNDEFINED;
			m_scan_start = 1;
			m_purge_rseg_truncate_frequency =
				ulint(srv_purge_rseg_truncate_frequency);
		}

		/** Clear the cached rollback segment. Normally done
		when purge is about to shutdown. */
		void clear()
		{
			reset();
			rseg_for_trunc_t	temp;
			m_rseg_for_trunc.swap(temp);
		}

		/** Is tablespace selected for truncate.
		@return true if undo tablespace is marked for truncate */
		bool is_marked() const
		{
			return(!(m_undo_for_trunc == ULINT_UNDEFINED));
		}

		/** Mark the tablespace for truncate.
		@param[in]	undo_id		tablespace for truncate. */
		void mark(ulint undo_id)
		{
			m_undo_for_trunc = undo_id;

			m_scan_start = (undo_id + 1)
					% (srv_undo_tablespaces_active + 1);
			if (m_scan_start == 0) {
				/* Note: UNDO tablespace ids starts from 1. */
				m_scan_start = 1;
			}

			/* We found an UNDO-tablespace to truncate so set the
			local purge rseg truncate frequency to 1. This will help
			accelerate the purge action and in turn truncate. */
			m_purge_rseg_truncate_frequency = 1;
		}

		/** Get the tablespace marked for truncate.
		@return tablespace id marked for truncate. */
		ulint get_marked_space_id() const
		{
			return(m_undo_for_trunc);
		}

		/** Add rseg to truncate vector.
		@param[in,out]	rseg	rseg for truncate */
		void add_rseg_to_trunc(trx_rseg_t* rseg)
		{
			m_rseg_for_trunc.push_back(rseg);
		}

		/** Get number of rsegs registered for truncate.
		@return return number of rseg that belongs to tablespace mark
		for truncate. */
		ulint rsegs_size() const
		{
			return(m_rseg_for_trunc.size());
		}

		/** Get ith registered rseg.
		@param[in]	id	index of rseg to get.
		@return reference to registered rseg. */
		trx_rseg_t* get_ith_rseg(ulint id)
		{
			ut_ad(id < m_rseg_for_trunc.size());
			return(m_rseg_for_trunc.at(id));
		}

		/** Reset for next rseg truncate. */
		void reset()
		{
			m_undo_for_trunc = ULINT_UNDEFINED;
			m_rseg_for_trunc.clear();

			/* Sync with global value as we are done with
			truncate now. */
			m_purge_rseg_truncate_frequency = static_cast<ulint>(
				srv_purge_rseg_truncate_frequency);
		}

		/** Get the tablespace id to start scanning from.
		@return	id of UNDO tablespace to start scanning from. */
		ulint get_scan_start() const
		{
			return(m_scan_start);
		}

		/** Check if the tablespace needs fix-up (based on presence of
		DDL truncate log)
		@param	space_id	space id of the undo tablespace to check
		@return true if fix up is needed else false */
		bool needs_fix_up(ulint	space_id) const
		{
			return(is_log_present(space_id));
		}

		/** Add undo tablespace to truncate vector.
		@param[in]	space_id	space id of tablespace to
						truncate */
		static void add_space_to_trunc_list(ulint space_id)
		{
			s_spaces_to_truncate.push_back(space_id);
		}

		/** Clear the truncate vector. */
		static void clear_trunc_list()
		{
			s_spaces_to_truncate.clear();
		}

		/** Is tablespace marked for truncate.
		@param[in]	space_id	space id to check
		@return true if marked for truncate, else false. */
		static bool is_tablespace_truncated(ulint space_id)
		{
			return(std::find(s_spaces_to_truncate.begin(),
					 s_spaces_to_truncate.end(), space_id)
			       != s_spaces_to_truncate.end());
		}

		/** Was a tablespace truncated at startup
		@param[in]	space_id	space id to check
		@return whether space_id was truncated at startup */
		static bool was_tablespace_truncated(ulint space_id)
		{
			return(std::find(s_fix_up_spaces.begin(),
					 s_fix_up_spaces.end(),
					 space_id)
			       != s_fix_up_spaces.end());
		}

		/** Get local rseg purge truncate frequency
		@return rseg purge truncate frequency. */
		ulint get_rseg_truncate_frequency() const
		{
			return(m_purge_rseg_truncate_frequency);
		}

		/* Start writing log information to a special file.
		On successfull completion, file is removed.
		On crash, file is used to complete the truncate action.
		@param	space_id	space id of undo tablespace
		@return DB_SUCCESS or error code. */
		dberr_t start_logging(ulint space_id)
		{
			return(init(space_id));
		}

		/* Mark completion of logging./
		@param	space_id	space id of undo tablespace */
		void done_logging(ulint space_id)
		{
			return(done(space_id));
		}

	private:
		/** UNDO tablespace is mark for truncate. */
		ulint			m_undo_for_trunc;

		/** rseg that resides in UNDO tablespace is marked for
		truncate. */
		rseg_for_trunc_t	m_rseg_for_trunc;

		/** Start scanning for UNDO tablespace from this space_id.
		This is to avoid bias selection of one tablespace always. */
		ulint			m_scan_start;

		/** Rollback segment(s) purge frequency. This is local
		value maintained along with global value. It is set to global
		value on start but when tablespace is marked for truncate it
		is updated to 1 and then minimum value among 2 is used by
		purge action. */
		ulint			m_purge_rseg_truncate_frequency;

		/** List of UNDO tablespace(s) to truncate. */
		static undo_spaces_t	s_spaces_to_truncate;
	public:
		/** Undo tablespaces that were truncated at startup */
		static undo_spaces_t	s_fix_up_spaces;
	};	/* class Truncate */

};	/* namespace undo */

/** The control structure used in the purge operation */
class purge_sys_t
{
  bool m_initialised;
public:
	MY_ALIGNED(CACHE_LINE_SIZE)
	rw_lock_t	latch;		/*!< The latch protecting the purge
					view. A purge operation must acquire an
					x-latch here for the instant at which
					it changes the purge view: an undo
					log operation can prevent this by
					obtaining an s-latch here. It also
					protects state and running */
	MY_ALIGNED(CACHE_LINE_SIZE)
	os_event_t	event;		/*!< State signal event;
					os_event_set() and os_event_reset()
					are protected by purge_sys_t::latch
					X-lock */
	MY_ALIGNED(CACHE_LINE_SIZE)
	ulint		n_stop;		/*!< Counter to track number stops */

	volatile bool	running;	/*!< true, if purge is active,
					we check this without the latch too */
	volatile purge_state_t	state;	/*!< Purge coordinator thread states,
					we check this in several places
					without holding the latch. */
	que_t*		query;		/*!< The query graph which will do the
					parallelized purge operation */
	MY_ALIGNED(CACHE_LINE_SIZE)
	ReadView	view;		/*!< The purge will not remove undo logs
					which are >= this view (purge view) */
	/** Total number of tasks submitted by srv_purge_coordinator_thread.
	Not accessed by other threads. */
	ulint	n_submitted;
	/** Number of completed tasks. Accessed by srv_purge_coordinator
	and srv_worker_thread by my_atomic. */
	ulint	n_completed;

	/** Iterator to the undo log records of committed transactions */
	struct iterator
	{
		bool operator<=(const iterator& other) const
		{
			if (commit < other.commit) return true;
			if (commit > other.commit) return false;
			return undo_no <= other.undo_no;
		}

		/** @return the commit number of the transaction */
		trx_id_t trx_no() const { return commit >> 1; }
		void reset_trx_no(trx_id_t trx_no) { commit = trx_no << 1; }

		/** 2 * trx_t::no + old_insert of the committed transaction */
		trx_id_t	commit;
		/** The record number within the committed transaction's undo
		log, increasing, purged from from 0 onwards */
		undo_no_t	undo_no;
	};

	/** The tail of the purge queue; the last parsed undo log of a
	committed transaction. */
	iterator	tail;
	/** The head of the purge queue; any older undo logs of committed
	transactions may be discarded (history list truncation). */
	iterator	head;
	/*-----------------------------*/
	bool		next_stored;	/*!< whether rseg holds the next record
					to purge */
	trx_rseg_t*	rseg;		/*!< Rollback segment for the next undo
					record to purge */
	ulint		page_no;	/*!< Page number for the next undo
					record to purge, page number of the
					log header, if dummy record */
	ulint		offset;		/*!< Page offset for the next undo
					record to purge, 0 if the dummy
					record */
	ulint		hdr_page_no;	/*!< Header page of the undo log where
					the next record to purge belongs */
	ulint		hdr_offset;	/*!< Header byte offset on the page */


	TrxUndoRsegsIterator
			rseg_iter;	/*!< Iterator to get the next rseg
					to process */

	purge_pq_t	purge_queue;	/*!< Binary min-heap, ordered on
					TrxUndoRsegs::trx_no. It is protected
					by the pq_mutex */
	PQMutex		pq_mutex;	/*!< Mutex protecting purge_queue */

	undo::Truncate	undo_trunc;	/*!< Track UNDO tablespace marked
					for truncate. */


  /**
    Constructor.

    Some members may require late initialisation, thus we just mark object as
    uninitialised. Real initialisation happens in create().
  */

  purge_sys_t() : m_initialised(false) {}


  bool is_initialised() const { return m_initialised; }


  /** Create the instance */
  void create();

  /** Close the purge system on shutdown */
  void close();
};

/** The global data structure coordinating a purge */
extern purge_sys_t	purge_sys;

/** Info required to purge a record */
struct trx_purge_rec_t {
	trx_undo_rec_t*	undo_rec;	/*!< Record to purge */
	roll_ptr_t	roll_ptr;	/*!< File pointr to UNDO record */
};

#include "trx0purge.ic"

#endif /* trx0purge_h */
