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

#include "ompl/kinematic/planners/kpiece/KPIECE1.h"
#include "ompl/base/GoalState.h"
#include <ros/console.h>

bool ompl::kinematic::KPIECE1::solve(double solveTime)
{
    SpaceInformationKinematic *si = dynamic_cast<SpaceInformationKinematic*>(m_si); 
    base::GoalRegion      *goal_r = dynamic_cast<base::GoalRegion*>(si->getGoal());
    GoalRegionKinematic   *goal_k = dynamic_cast<GoalRegionKinematic*>(si->getGoal());
    base::GoalState       *goal_s = dynamic_cast<base::GoalState*>(si->getGoal());
    unsigned int              dim = si->getStateDimension();
    
    if (!goal_s && !goal_r)
    {
	ROS_ERROR("KPIECE1: Unknown type of goal (or goal undefined)");
	return false;
    }
    
    ros::WallTime endTime = ros::WallTime::now() + ros::WallDuration(solveTime);

    if (m_tree.grid.size() == 0)
    {
	for (unsigned int i = 0 ; i < m_si->getStartStateCount() ; ++i)
	{
	    Motion *motion = new Motion(dim);
	    si->copyState(motion->state, si->getStartState(i));
	    if (si->satisfiesBounds(motion->state) && si->isValid(motion->state))
		addMotion(motion, 1.0);
	    else
	    {
		ROS_ERROR("KPIECE1: Initial state is invalid!");
		delete motion;
	    }	
	}
    }
    
    if (m_tree.grid.size() == 0)
    {
	ROS_ERROR("KPIECE1: There are no valid initial states!");
	return false;	
    }    

    ROS_INFO("KPIECE1: Starting with %u states", m_tree.size);
    
    std::vector<double> range(dim);
    for (unsigned int i = 0 ; i < dim ; ++i)
	range[i] = m_rho * (si->getStateComponent(i).maxValue - si->getStateComponent(i).minValue);
    
    Motion *solution  = NULL;
    Motion *approxsol = NULL;
    double  approxdif = INFINITY;
    base::State *xstate = new base::State(dim);

    double improveValue = 0.01;

    while (ros::WallTime::now() < endTime)
    {
	m_tree.iteration++;
	
	/* Decide on a state to expand from */
	Motion     *existing = NULL;
	Grid::Cell *ecell = NULL;
	selectMotion(existing, ecell);
	assert(existing);
	
	/* sample random state (with goal biasing) */
	if (m_rng.uniform01() < m_goalBias)
	{
	    if (goal_s)
		si->copyState(xstate, goal_s->state);
	    else
		if (goal_k)
		    goal_k->sampleNearGoal(xstate);
		else
		{
		    if (approxsol)
		    {
			si->copyState(xstate, approxsol->state);
			ROS_DEBUG("Start Running HCIK (%f)...", improveValue);			
			if (!m_hcik.tryToImprove(xstate, improveValue))
			{
			    m_sCore->sampleNear(xstate, existing->state, range);
			    improveValue /= 2.0;
			}
			ROS_DEBUG("End Running HCIK");			
		    }
		    else
			m_sCore->sampleNear(xstate, existing->state, range);
		}
	}
	else
	    m_sCore->sampleNear(xstate, existing->state, range);
	
	double failTime = 0.0;
	bool keep = si->checkMotion(existing->state, xstate, xstate, &failTime);
	if (!keep && failTime > m_minValidPathPercentage)
	    keep = true;
	
	if (keep)
	{
	    /* create a motion */
	    Motion *motion = new Motion(dim);
	    si->copyState(motion->state, xstate);
	    motion->parent = existing;

	    double dist = 0.0;
	    bool solved = goal_r->isSatisfied(motion->state, &dist);
	    addMotion(motion, dist);
	    
	    if (solved)
	    {
		approxdif = dist;
		solution = motion;    
		break;
	    }
	    if (dist < approxdif)
	    {
		approxdif = dist;
		approxsol = motion;
	    }
	    ecell->data->score *= m_goodScoreFactor;
	}
	else
	    ecell->data->score *= m_badScoreFactor;
	
	m_tree.grid.update(ecell);
    }
    
    bool approximate = false;
    if (solution == NULL)
    {	
	solution = approxsol;
	approximate = true;
    }
    
    if (solution != NULL)
    {
	/* construct the solution path */
	std::vector<Motion*> mpath;
	while (solution != NULL)
	{
	    mpath.push_back(solution);
	    solution = solution->parent;
	}

	/* set the solution path */
	PathKinematic *path = new PathKinematic(m_si);
   	for (int i = mpath.size() - 1 ; i >= 0 ; --i)
	{   
	    base::State *st = new base::State(dim);
	    si->copyState(st, mpath[i]->state);
	    path->states.push_back(st);
	}
	goal_r->setDifference(approxdif);
	goal_r->setSolutionPath(path, approximate);

	if (approximate)
	    ROS_WARN("KPIECE1: Found approximate solution");
    }

    delete xstate;
    
    ROS_INFO("KPIECE1: Created %u states in %u cells (%u internal + %u external)", m_tree.size, m_tree.grid.size(),
	     m_tree.grid.countInternal(), m_tree.grid.countExternal());
    
    return goal_r->isAchieved();
}

bool ompl::kinematic::KPIECE1::selectMotion(Motion* &smotion, Grid::Cell* &scell)
{
    scell = m_rng.uniform01() < std::max(m_selectBorderPercentage, m_tree.grid.fracExternal()) ?
	m_tree.grid.topExternal() : m_tree.grid.topInternal();
    if (scell && !scell->data->motions.empty())
    {
	scell->data->selections++;
	smotion = scell->data->motions[m_rng.halfNormalInt(0, scell->data->motions.size() - 1)];
	return true;
    }
    else
	return false;
}

unsigned int ompl::kinematic::KPIECE1::addMotion(Motion *motion, double dist)
{
    Grid::Coord coord;
    m_projectionEvaluator->computeCoordinates(motion->state, coord);
    Grid::Cell* cell = m_tree.grid.getCell(coord);
    unsigned int created = 0;
    if (cell)
    {
	cell->data->motions.push_back(motion);
	cell->data->coverage += 1.0;
	m_tree.grid.update(cell);
    }
    else
    {
	cell = m_tree.grid.createCell(coord);
	cell->data = new CellData();
	cell->data->motions.push_back(motion);
	cell->data->coverage = 1.0;
	cell->data->iteration = m_tree.iteration;
	cell->data->selections = 1;
	cell->data->score = 1.0 / (1e-3 + dist);
	m_tree.grid.add(cell);
	created = 1;
    }
    m_tree.size++;
    return created;
}

void ompl::kinematic::KPIECE1::getStates(std::vector<const base::State*> &states) const
{
    states.resize(0);
    states.reserve(m_tree.size);
    
    std::vector<CellData*> cdata;
    m_tree.grid.getContent(cdata);
    for (unsigned int i = 0 ; i < cdata.size() ; ++i)
	for (unsigned int j = 0 ; j < cdata[i]->motions.size() ; ++j)
	    states.push_back(cdata[i]->motions[j]->state); 
}
