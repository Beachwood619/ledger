/*
 * Copyright (c) 2003-2009, John Wiegley.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 *
 * - Neither the name of New Artisans LLC nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "chain.h"
#include "report.h"
#include "filters.h"

namespace ledger {

xact_handler_ptr chain_xact_handlers(report_t&	      report,
				     xact_handler_ptr base_handler,
				     const bool	      handle_individual_xacts)
{
  xact_handler_ptr handler(base_handler);

  // format_xacts write each xact received to the output stream.
  if (handle_individual_xacts) {
    // truncate_entries cuts off a certain number of _entries_ from being
    // displayed.  It does not affect calculation.
    if (report.HANDLED(head_) || report.HANDLED(tail_))
      handler.reset(new truncate_entries(handler,
					 report.HANDLER(head_).value.to_long(),
					 report.HANDLER(tail_).value.to_long()));

    // filter_xacts will only pass through xacts matching the
    // `display_predicate'.
    if (report.HANDLED(display_))
      handler.reset(new filter_xacts
		    (handler, item_predicate<xact_t>(report.HANDLER(display_).str(),
						     report.what_to_keep())));

    // calc_xacts computes the running total.  When this appears will
    // determine, for example, whether filtered xacts are included or excluded
    // from the running total.
    assert(report.HANDLED(amount_));
    expr_t& expr(report.HANDLER(amount_).expr);
    expr.set_context(&report);
    handler.reset(new calc_xacts(handler, expr));

    // filter_xacts will only pass through xacts matching the
    // `secondary_predicate'.
    if (report.HANDLED(only_))
      handler.reset(new filter_xacts
		    (handler, item_predicate<xact_t>
		     (report.HANDLER(only_).str(), report.what_to_keep())));

    // sort_xacts will sort all the xacts it sees, based on the `sort_order'
    // value expression.
    if (report.HANDLED(sort_)) {
      if (report.HANDLED(sort_entries_))
	handler.reset(new sort_entries(handler, report.HANDLER(sort_).str()));
      else
	handler.reset(new sort_xacts(handler, report.HANDLER(sort_).str()));
    }

    // changed_value_xacts adds virtual xacts to the list to account for
    // changes in market value of commodities, which otherwise would affect
    // the running total unpredictably.
    if (report.HANDLED(revalued))
      handler.reset(new changed_value_xacts(handler,
					    report.HANDLER(total_).expr,
					    report.HANDLED(revalued_only)));

    // collapse_xacts causes entries with multiple xacts to appear as entries
    // with a subtotaled xact for each commodity used.
    if (report.HANDLED(collapse))
      handler.reset(new collapse_xacts(handler, report.session));

    // subtotal_xacts combines all the xacts it receives into one subtotal
    // entry, which has one xact for each commodity in each account.
    //
    // period_xacts is like subtotal_xacts, but it subtotals according to time
    // periods rather than totalling everything.
    //
    // dow_xacts is like period_xacts, except that it reports all the xacts
    // that fall on each subsequent day of the week.
    if (report.HANDLED(subtotal))
      handler.reset(new subtotal_xacts(handler));

    if (report.HANDLED(dow))
      handler.reset(new dow_xacts(handler));
    else if (report.HANDLED(by_payee))
      handler.reset(new by_payee_xacts(handler));

    // interval_xacts groups xacts together based on a time period, such as
    // weekly or monthly.
    if (report.HANDLED(period_)) {
      handler.reset(new interval_xacts(handler, report.HANDLER(period_).str()));
      handler.reset(new sort_xacts(handler, "d"));
    }
  }

  // invert_xacts inverts the value of the xacts it receives.
  if (report.HANDLED(invert))
    handler.reset(new invert_xacts(handler));

  // related_xacts will pass along all xacts related to the xact received.  If
  // the `related_all' handler is on, then all the entry's xacts are passed;
  // meaning that if one xact of an entry is to be printed, all the xact for
  // that entry will be printed.
  if (report.HANDLED(related))
    handler.reset(new related_xacts(handler, report.HANDLED(related_all)));

  // anonymize_xacts removes all meaningful information from entry payee's and
  // account names, for the sake of creating useful bug reports.
  if (report.HANDLED(anon))
    handler.reset(new anonymize_xacts(handler));

  // This filter_xacts will only pass through xacts matching the `predicate'.
  if (report.HANDLED(limit_)) {
    DEBUG("report.predicate",
	  "Report predicate expression = " << report.HANDLER(limit_).str());
    handler.reset(new filter_xacts
		  (handler, item_predicate<xact_t>(report.HANDLER(limit_).str(),
						   report.what_to_keep())));
  }

#if 0
  // budget_xacts takes a set of xacts from a data file and uses them to
  // generate "budget xacts" which balance against the reported xacts.
  //
  // forecast_xacts is a lot like budget_xacts, except that it adds entries
  // only for the future, and does not balance them against anything but the
  // future balance.

  if (report.budget_flags) {
    budget_xacts * budget_handler = new budget_xacts(handler,
						     report.budget_flags);
    budget_handler->add_period_entries(journal->period_entries);
    handler.reset(budget_handler);

    // Apply this before the budget handler, so that only matching xacts are
    // calculated toward the budget.  The use of filter_xacts above will
    // further clean the results so that no automated xacts that don't match
    // the filter get reported.
    if (! report.predicate.empty())
      handler.reset(new filter_xacts(handler, report.predicate));
  }
  else if (! report.forecast_limit.empty()) {
    forecast_xacts * forecast_handler
      = new forecast_xacts(handler, report.forecast_limit);
    forecast_handler->add_period_entries(journal->period_entries);
    handler.reset(forecast_handler);

    // See above, under budget_xacts.
    if (! report.predicate.empty())
      handler.reset(new filter_xacts(handler, report.predicate));
  }
#endif

  if (report.HANDLED(comm_as_payee))
    handler.reset(new set_comm_as_payee(handler));
  else if (report.HANDLED(code_as_payee))
    handler.reset(new set_code_as_payee(handler));

  return handler;
}

} // namespace ledger
