/**
 * Copyright (c) 2011 10gen Inc.
 *
 * This program is free software: you can redistribute it and/or  modify
 * it under the terms of the GNU Affero General Public License, version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "pch.h"
#include "accumulator.h"

#include "db/pipeline/value.h"

namespace mongo {
    boost::shared_ptr<const Value> AccumulatorMinMax::evaluate(
        boost::shared_ptr<Document> pDocument) const {
        assert(vpOperand.size() == 1);
	boost::shared_ptr<const Value> prhs(vpOperand[0]->evaluate(pDocument));

        /* if this is the first value, just use it */
        if (!pValue.get())
            pValue = prhs;
        else {
            /* compare with the current value; swap if appropriate */
            int cmp = Value::compare(pValue, prhs) * sense;
            if (cmp > 0)
                pValue = prhs;
        }

        return pValue;
    }

    boost::shared_ptr<const Value> AccumulatorMinMax::getValue() const {
        return pValue;
    }

    AccumulatorMinMax::AccumulatorMinMax(int theSense):
        sense(theSense),
        pValue(boost::shared_ptr<const Value>()) {
        assert((sense == 1) || (sense == -1)); // CW TODO error
    }

    boost::shared_ptr<Accumulator> AccumulatorMinMax::createMin() {
	boost::shared_ptr<AccumulatorMinMax> pAccumulator(
	    new AccumulatorMinMax(1));
        return pAccumulator;
    }

    boost::shared_ptr<Accumulator> AccumulatorMinMax::createMax() {
	boost::shared_ptr<AccumulatorMinMax> pAccumulator(
	    new AccumulatorMinMax(-1));
        return pAccumulator;
    }

    const char *AccumulatorMinMax::getOpName() const {
	if (sense == 1)
	    return "$min";
	return "$max";
    }
}
