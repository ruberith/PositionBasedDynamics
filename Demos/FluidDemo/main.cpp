#include "Common/Common.h"
#include "Demos/Visualization/MiniGL.h"
#include "Demos/Visualization/Selection.h"
#include "Simulation/TimeManager.h"
#include "Demos/Common/DemoBase.h"
#include <Eigen/Dense>
#include "FluidModel.h"
#include "TimeStepFluidModel.h"
#include "Simulation/Simulation.h"
#include <iostream>
#include "Utils/Logger.h"
#include "Utils/Timing.h"
#include "Utils/FileSystem.h"
#include "../Common/imguiParameters.h"
#define _USE_MATH_DEFINES
#include "math.h"


// Enable memory leak detection
#if defined(_DEBUG) && !defined(EIGEN_ALIGN)
	#define new DEBUG_NEW 
#endif

using namespace PBD;
using namespace std;
using namespace Utilities;

void timeStep ();
void buildModel ();
void createBreakingDam();
void addWall(const Vector3r &minX, const Vector3r &maxX, std::vector<Vector3r> &boundaryParticles);
void initBoundaryData(std::vector<Vector3r> &boundaryParticles);
void render ();
void cleanup();
void reset();
void selection(const Vector2i &start, const Vector2i &end, void *clientData);


FluidModel model;
TimeStepFluidModel simulation;
DemoBase* base;

const Real particleRadius = static_cast<Real>(0.025);
const unsigned int width = 15;
const unsigned int depth = 15;
const unsigned int height = 20;
const Real containerWidth = (width + 1)*particleRadius*static_cast<Real>(2.0 * 5.0);
const Real containerDepth = (depth + 1)*particleRadius*static_cast<Real>(2.0);
const Real containerHeight = 4.0;
bool doPause = true;
std::vector<unsigned int> selectedParticles;
Vector3r oldMousePos;
string exePath;
string dataPath;


// main 
int main( int argc, char **argv )
{
	REPORT_MEMORY_LEAKS

	base = new DemoBase();
	base->init(argc, argv, "Fluid demo");

	// we use an own time step controller
	delete PBD::Simulation::getCurrent()->getTimeStep();
	PBD::Simulation::getCurrent()->setTimeStep(nullptr);

	MiniGL::setSelectionFunc(selection, nullptr);
	MiniGL::setClientIdleFunc(timeStep);
	MiniGL::addKeyFunc('r', reset);
	MiniGL::setClientSceneFunc(render);
	MiniGL::setViewport (40.0, 0.1f, 500.0, Vector3r (0.0, 3.0, 8.0), Vector3r (0.0, 0.0, 0.0));

	buildModel();

	base->createParameterGUI();

	// add additional parameter just for this demo
	imguiParameters::imguiEnumParameter* eparam = new imguiParameters::imguiEnumParameter();
	eparam->description = "Velocity update method";
	eparam->label = "Velocity update method";
	eparam->getFct = [&]() -> int { return simulation.getVelocityUpdateMethod(); };
	eparam->setFct = [&](int i) -> void { simulation.setVelocityUpdateMethod(i); };
	eparam->items.push_back("First Order Update");
	eparam->items.push_back("Second Order Update");
	imguiParameters::addParam("Simulation", "PBD", eparam);

	imguiParameters::imguiNumericParameter<Real>* param = new imguiParameters::imguiNumericParameter<Real>();
	param->description = "Viscosity coefficient";
	param->label = "Viscosity";
	param->getFct = [&]() -> Real { return model.getViscosity(); };
	param->setFct = [&](Real v) -> void { model.setViscosity(v); };
	imguiParameters::addParam("Simulation", "PBD", param);

	MiniGL::mainLoop();	

	cleanup ();
	base->cleanup();

	Utilities::Timing::printAverageTimes();
	Utilities::Timing::printTimeSums();
	delete base;
	delete Simulation::getCurrent();

	return 0;
}

void cleanup()
{
	delete TimeManager::getCurrent();
}

void reset()
{
	Timing::printAverageTimes();
	Timing::reset();

	model.reset();
	simulation.reset();
	TimeManager::getCurrent()->setTime(0.0);
}

void mouseMove(int x, int y, void *clientData)
{
	Vector3r mousePos;
	MiniGL::unproject(x, y, mousePos);
	const Vector3r diff = mousePos - oldMousePos;

	TimeManager *tm = TimeManager::getCurrent();
	const Real h = tm->getTimeStepSize();

	ParticleData &pd = model.getParticles();
	for (unsigned int j = 0; j < selectedParticles.size(); j++)
	{
		pd.getVelocity(selectedParticles[j]) += 5.0*diff/h;
	}
	oldMousePos = mousePos;
}

void selection(const Vector2i &start, const Vector2i &end, void *clientData)
{
	std::vector<unsigned int> hits;
	selectedParticles.clear();
	ParticleData &pd = model.getParticles();
	Selection::selectRect(start, end, &pd.getPosition(0), &pd.getPosition(pd.size() - 1), selectedParticles);
	if (selectedParticles.size() > 0)
		MiniGL::setMouseMoveFunc(2, mouseMove);
	else
		MiniGL::setMouseMoveFunc(-1, NULL);

	MiniGL::unproject(end[0], end[1], oldMousePos);
}

void timeStep ()
{
	const Real pauseAt = base->getValue<Real>(DemoBase::PAUSE_AT);
	if ((pauseAt > 0.0) && (pauseAt < TimeManager::getCurrent()->getTime()))
		base->setValue(DemoBase::PAUSE, true);

	if (base->getValue<bool>(DemoBase::PAUSE))
		return;

	// Simulation code
	const unsigned int numSteps = base->getValue<unsigned int>(DemoBase::NUM_STEPS_PER_RENDER);
	for (unsigned int i = 0; i < numSteps; i++)
	{
		START_TIMING("SimStep");
		simulation.step(model);
		STOP_TIMING_AVG;

		base->step();
	}
}

void buildModel ()
{
	TimeManager::getCurrent ()->setTimeStepSize (static_cast<Real>(0.0025));

	createBreakingDam();
}

void render ()
{
	// Draw simulation model
	
	const ParticleData &pd = model.getParticles();
	const unsigned int nParticles = pd.size();

	const Real supportRadius = model.getSupportRadius();
	Real vmax = static_cast<Real>(0.4*2.0)*supportRadius / TimeManager::getCurrent()->getTimeStepSize();
	Real vmin = 0.0;

	for (unsigned int i = 0; i < nParticles; i++)
	{
		Real v = pd.getVelocity(i).norm();
		v = static_cast<Real>(0.5)*((v - vmin) / (vmax - vmin));
		v = min(static_cast<Real>(128.0)*v*v, static_cast<Real>(0.5));
		float fluidColor[4] = { 0.2f, 0.2f, 0.2f, 1.0 };
		MiniGL::hsvToRgb(0.55f, 1.0f, 0.5f + (float)v, fluidColor);
		MiniGL::drawSphere(pd.getPosition(i), particleRadius, fluidColor, 8);
	}

//	float surfaceColor[4] = { 0.2f, 0.6f, 0.8f, 1 };
//	for (unsigned int i = 0; i < model.numBoundaryParticles(); i++)
//		MiniGL::drawSphere(model.getBoundaryX(i), particleRadius, surfaceColor, 8);

	float red[4] = { 0.8f, 0.0f, 0.0f, 1 };
	for (unsigned int j = 0; j < selectedParticles.size(); j++)
	{
		MiniGL::drawSphere(pd.getPosition(selectedParticles[j]), 0.08f, red);
	}

	base->render();

}


/** Create a breaking dam scenario
*/
void createBreakingDam()
{
	LOG_INFO << "Initialize fluid particles";
	const Real diam = 2.0*particleRadius;
	const Real startX = -static_cast<Real>(0.5)*containerWidth + diam;
	const Real startY = diam;
	const Real startZ = -static_cast<Real>(0.5)*containerDepth + diam;
	const Real yshift = sqrt(static_cast<Real>(3.0)) * particleRadius;

	std::vector<Vector3r> fluidParticles;
	fluidParticles.resize(width*height*depth);

	#pragma omp parallel default(shared)
	{
		#pragma omp for schedule(static)  
		for (int i = 0; i < (int)width; i++)
		{
			for (unsigned int j = 0; j < height; j++)
			{
				for (unsigned int k = 0; k < depth; k++)
				{
					fluidParticles[i*height*depth + j*depth + k] = diam*Vector3r((Real)i, (Real)j, (Real)k) + Vector3r(startX, startY, startZ);
				}
			}
		}
	}

	model.setParticleRadius(particleRadius);

	std::vector<Vector3r> boundaryParticles;
	initBoundaryData(boundaryParticles);

	model.initModel((unsigned int)fluidParticles.size(), fluidParticles.data(), (unsigned int)boundaryParticles.size(), boundaryParticles.data());

	LOG_INFO << "Number of particles: " << width*height*depth;
}


void addWall(const Vector3r &minX, const Vector3r &maxX, std::vector<Vector3r> &boundaryParticles)
{
	const Real particleDistance = static_cast<Real>(2.0)*model.getParticleRadius();

	const Vector3r diff = maxX - minX;
	const unsigned int stepsX = (unsigned int)(diff[0] / particleDistance) + 1u;
	const unsigned int stepsY = (unsigned int)(diff[1] / particleDistance) + 1u;
	const unsigned int stepsZ = (unsigned int)(diff[2] / particleDistance) + 1u;

	const unsigned int startIndex = (unsigned int) boundaryParticles.size();
	boundaryParticles.resize(startIndex + stepsX*stepsY*stepsZ);

	#pragma omp parallel default(shared)
	{
		#pragma omp for schedule(static)  
		for (int j = 0; j < (int)stepsX; j++)
		{
			for (unsigned int k = 0; k < stepsY; k++)
			{
				for (unsigned int l = 0; l < stepsZ; l++)
				{
					const Vector3r currPos = minX + Vector3r(j*particleDistance, k*particleDistance, l*particleDistance);
					boundaryParticles[startIndex + j*stepsY*stepsZ + k*stepsZ + l] = currPos;
				}
			}
		}
	}
}

void initBoundaryData(std::vector<Vector3r> &boundaryParticles)
{
	const Real x1 = -containerWidth / 2.0;
	const Real x2 = containerWidth / 2.0;
	const Real y1 = 0.0;
	const Real y2 = containerHeight;
	const Real z1 = -containerDepth / 2.0;
	const Real z2 = containerDepth / 2.0;

	const Real diam = 2.0*particleRadius;

	// Floor
	addWall(Vector3r(x1, y1, z1), Vector3r(x2, y1, z2), boundaryParticles);
	// Top
	addWall(Vector3r(x1, y2, z1), Vector3r(x2, y2, z2), boundaryParticles);
	// Left
	addWall(Vector3r(x1, y1, z1), Vector3r(x1, y2, z2), boundaryParticles);
	// Right
	addWall(Vector3r(x2, y1, z1), Vector3r(x2, y2, z2), boundaryParticles);
	// Back
	addWall(Vector3r(x1, y1, z1), Vector3r(x2, y2, z1), boundaryParticles);
	// Front
	addWall(Vector3r(x1, y1, z2), Vector3r(x2, y2, z2), boundaryParticles);
}
