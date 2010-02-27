/*********************************************************************
* Software License Agreement (BSD License)
* 
*  Copyright (c) 2008, Willow Garage, Inc.
*  All rights reserved.
* 
*  Redistribution and use in source and binary forms, with or without
*  modification, are permitted provided that the following conditions
*  are met:
* 
*   * Redistributions of source code must retain the above copyright
*     notice, this list of conditions and the following disclaimer.
*   * Redistributions in binary form must reproduce the above
*     copyright notice, this list of conditions and the following
*     disclaimer in the documentation and/or other materials provided
*     with the distribution.
*   * Neither the name of the Willow Garage nor the names of its
*     contributors may be used to endorse or promote products derived
*     from this software without specific prior written permission.
* 
*  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
*  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
*  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
*  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
*  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
*  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
*  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
*  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
*  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
*  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
*  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
*  POSSIBILITY OF SUCH DAMAGE.
*********************************************************************/

/* \author Ioan Sucan */

#include "ompl/extension/kinematic/SpaceInformationKinematic.h"
#include <angles/angles.h>
#include <algorithm>
#include <queue>
#include <cassert>

void ompl::kinematic::SpaceInformationKinematic::setup(void)
{
    assert(m_stateDistanceEvaluator);
    assert(m_stateValidityChecker);
    SpaceInformation::setup();
}

bool ompl::kinematic::SpaceInformationKinematic::checkMotionSubdivision(const base::State *s1, const base::State *s2) const
{
    /* assume motion starts in a valid configuration so s1 is valid */
    if (!isValid(s2))
	return false;
    
    std::valarray<double> step;
    int nd = findDifferenceStep(s1, s2, 1.0, step);
    
    /* initialize the queue of test positions */
    std::queue< std::pair<int, int> > pos;
    if (nd >= 2)
	pos.push(std::make_pair(1, nd - 1));
    
    /* temporary storage for the checked state */
    base::State test(m_stateDimension);
    
    /* repeatedly subdivide the path segment in the middle (and check the middle) */
    while (!pos.empty())
    {
	std::pair<int, int> x = pos.front();
	
	int mid = (x.first + x.second) / 2;
	
	for (unsigned int j = 0 ; j < m_stateDimension ; ++j)
	    test.values[j] = s1->values[j] + (double)mid * step[j];
	
	if (!isValid(&test))
	    return false;
	
	pos.pop();
	
	if (x.first < mid)
	    pos.push(std::make_pair(x.first, mid - 1));
	if (x.second > mid)
	    pos.push(std::make_pair(mid + 1, x.second));
    }
    
    return true;
}

bool ompl::kinematic::SpaceInformationKinematic::checkMotionIncremental(const base::State *s1, const base::State *s2,
									base::State *lastValidState, double *lastValidTime) const
{   
    /* assume motion starts in a valid configuration so s1 is valid */
    if (!isValid(s2))
	return false;
    
    std::valarray<double> step;
    int nd = findDifferenceStep(s1, s2, 1.0, step);
    
    /* temporary storage for the checked state */
    base::State test(m_stateDimension);
    
    for (int j = 1 ; j < nd ; ++j)
    {
	double factor = (double)j;
	for (unsigned int k = 0 ; k < m_stateDimension ; ++k)
	    test.values[k] = s1->values[k] + factor * step[k];
	if (!isValid(&test))
	{
	    if (lastValidState)
	    {
		factor -= 1.0;
		for (unsigned int k = 0 ; k < m_stateDimension ; ++k)
		    lastValidState->values[k] = s1->values[k] + factor * step[k];
	    }
	    if (lastValidTime)
		*lastValidTime = (double)(j - 1) / (double)nd;
	    return false;
	}
    }
    
    return true;
}

bool ompl::kinematic::SpaceInformationKinematic::checkPath(const PathKinematic *path) const
{
    bool result = path != NULL;
    if (result && path->states.size() > 0)
    {
	if (isValid(path->states[0]))
	{
	    int last = path->states.size() - 1;
	    for (int j = 0 ; result && j < last ; ++j)
		if (!checkMotionSubdivision(path->states[j], path->states[j + 1]))
		    result = false;
	}
	else
	    result = false;
    }
    return result;
}

void ompl::kinematic::SpaceInformationKinematic::interpolatePath(PathKinematic *path, double factor) const
{
    std::vector<base::State*> newStates;
    const int n1 = path->states.size() - 1;
    
    for (int i = 0 ; i < n1 ; ++i)
    {
	base::State *s1 = path->states[i];
	base::State *s2 = path->states[i + 1];
	
	newStates.push_back(s1);
	
	std::valarray<double> step;
	int nd = findDifferenceStep(s1, s2, factor, step);
	
	/* find the states in between */
	for (int j = 1 ; j < nd ; ++j)
	{
	    base::State *state = new base::State(m_stateDimension);
	    for (unsigned int k = 0 ; k < m_stateDimension ; ++k)
		state->values[k] = s1->values[k] + (double)j * step[k];
	    newStates.push_back(state);
	}
    }
    newStates.push_back(path->states[n1]);
    
    path->states.swap(newStates);
}

unsigned int ompl::kinematic::SpaceInformationKinematic::getMotionStates(const base::State *s1, const base::State *s2, std::vector<base::State*> &states, bool alloc) const
{
    std::valarray<double> step;
    int nd = findDifferenceStep(s1, s2, 1.0, step);
    if (alloc)
    {
	states.resize(nd + 1);
	states[0] = new base::State(m_stateDimension);
    }
    
    unsigned int added = 0;
    
    if (states.size() > 0)
    {
	copyState(states[0], s1);
	added++;
    }
    
    /* find the states in between */
    for (int j = 1 ; j < nd && added < states.size() ; ++j)
    {
	added++;
	if (alloc)
	    states[j] = new base::State(m_stateDimension);
	base::State *state = states[j];
	for (unsigned int k = 0 ; k < m_stateDimension ; ++k)
	    state->values[k] = s1->values[k] + (double)j * step[k];
    }
    
    if (added < states.size())
    {
	if (alloc)
	    states[added] = new base::State(m_stateDimension);
	copyState(states[added], s2);
	added++;
    }
    
    return added;
}

int ompl::kinematic::SpaceInformationKinematic::findDifferenceStep(const base::State *s1, const base::State *s2, double factor,
								   std::valarray<double> &step) const
{
    /* find diffs */
    std::valarray<double> diff(m_stateDimension);
    for (unsigned int i = 0 ; i < m_stateDimension ; ++i)
	diff[i] = m_stateComponent[i].type == base::StateComponent::WRAPPING_ANGLE ? 
	    angles::shortest_angular_distance(s1->values[i], s2->values[i]) : s2->values[i] - s1->values[i];
    
    // will need to handle quaternions; use bullet LinearMath? use slerp?
    
    /* find out how many divisions we need */
    int nd = 1;
    for (unsigned int i = 0 ; i < m_stateDimension ; ++i)
    {
	int d = 1 + (int)(fabs(diff[i]) / (factor * m_stateComponent[i].resolution));
	if (nd < d)
	    nd = d;
    }
    
    /* find out the step size as a vector */
    step.resize(m_stateDimension); 
    step = diff / (double)nd;
    
    return nd;
}

