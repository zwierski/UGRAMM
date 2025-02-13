//===================================================================//
// Universal GRAaph Minor Mapping (UGRAMM) method for CGRA           //
// file : UGRAMM.cpp                                                 //
// description: contains primary functions                           //
//===================================================================//

#include "../lib/UGRAMM.h"
#include "../lib/utilities.h"
#include "../lib/routing.h"
#include "../lib/drc.h"

//-------------------------------------------------------------------//
//------------------- Declartion of variables -----------------------//
//-------------------------------------------------------------------//

// Pathefinder cost parameters:
int iterCount = 0;    //Number of iteartions pathfinder will run for. 
float PFac = 1;       // Congestion cost factor
float HFac = 1;       // History cost factor
float pfac_mul = 1.1; //Multiplier for present congestion cost [defaults to 1.1]
float hfac_mul = 1;   //Multiplier for history congestion cost [defaults to 1]
float base_cost = 1;  //Base cost of using a wire-segment in Pathfinder!!
int capacity = 1;     //One wire can be routed via a segment in CGRA

// Mapping related variables:
int max_iter = 40;                      //Initializing maximum number of iterations of UGRAMM to 40 initially.
std::vector<RoutingTree> *Trees;
std::vector<std::list<int>> *Users;
std::map<int, int> invUsers;
std::vector<int> *HistoryCosts;
std::vector<int> *TraceBack;
std::vector<int> *TopoOrder;
std::map<int, std::string> hNames;
std::map<std::string, int> hNamesInv;
std::map<int, std::string> gNames;
std::map<std::string, int> gNamesInv;
std::map<std::string, int> gNamesInv_FuncCell;
std::bitset<100000> explored;

std::vector<std::string> inPin = {"inPinA", "inPinB", "anyPins"};
std::vector<std::string> outPin = {"outPinA"};

// Logger & CLI global variables:
std::shared_ptr<spdlog::logger> UGRAMM = spdlog::stdout_color_mt("UGRAMM");
std::shared_ptr<spdlog::logger> drcLogger = spdlog::stdout_color_mt("DRC Checks");
namespace po = boost::program_options;

//ugrammConfig structure which is parsed from Pragma's of Device and application graph
std::map<std::string, std::vector<std::string>> ugrammConfig; 

// Defining the JSON for the config file:
json jsonParsed;
json UgrammPragmaConfig;

//---------------------------------------------------------------------//

/**
 * Finds the minimal vertex model for embedding.
 */
int findMinVertexModel(DirectedGraph *G, DirectedGraph *H, int y, std::map<int, NodeConfig> *hConfig, std::map<int, NodeConfig> *gConfig)
{
  //--------------------------------------
  // Step 1: Checking all ϕ(xj) are empty
  //--------------------------------------
  bool allEmpty = true;

  in_edge_iterator ei, ei_end;
  vertex_descriptor yD = vertex(y, *H);
  boost::tie(ei, ei_end) = in_edges(yD, *H);
  for (; ei != ei_end; ei++)
  {
    if (invUsers[source(*ei, *G)] != NOT_PLACED) //invUsers hold the information whether the current HID is placed or not
      allEmpty = false;
  }

  out_edge_iterator eo, eo_end;
  boost::tie(eo, eo_end) = out_edges(yD, *H);
  for (; eo != eo_end; eo++)
  {
    if (invUsers[target(*eo, *H)] != NOT_PLACED) //invUsers hold the information whether the current HID is placed or not
      allEmpty = false;
  }

  if (allEmpty)
  {
    // choose a random unused node of G to be the vertex model for y because NONE of its fanins or fanouts have vertex models
    int chooseRand = (rand() % num_vertices(*G));
    bool vertex_found = false;

    while (vertex_found == false)
    {
      chooseRand = (chooseRand + 1) % num_vertices(*G);
      if (compatibilityCheck(chooseRand, y, hConfig, gConfig))
      {
        if ((*Users)[chooseRand].size() == 0)
        {
          vertex_found = true;
        }
      }
    }

    UGRAMM->debug("[RandomSelection] for application node :: {} :: Choosing starting vertex model as :: {}", hNames[y], gNames[chooseRand]);

    //--- InvUsers and Users registering ----//
    invUsers[y] = chooseRand;
    (*Users)[chooseRand].push_back(y);
    //---------------------------------------//

    return 0;
  }

  // At least one of y's fanins or fanouts have a vertex model [not allEmpty cases]
  int bestCost = MAX_DIST;
  int bestIndexFuncCell = -1;

  int totalCosts[num_vertices(*G)];
  for (int i = 0; i < num_vertices(*G); i++)
    totalCosts[i] = 0;

  bool compatibilityStatus = true;

  UGRAMM->debug("[Locking] LockGNode {} :: Not Empty {}", (*hConfig)[y].LockGNode, !(*hConfig)[y].LockGNode.empty());
  bool lockingNodeStatus = !(*hConfig)[y].LockGNode.empty();

  if (lockingNodeStatus){
    std::vector<int> suitableGIDs;

    findGNodeID_FuncCell((*hConfig)[y].LockGNode, suitableGIDs);
    
    if (UGRAMM->level() <= spdlog::level::trace){
      for (int i = 0; i < suitableGIDs.size(); i++){
        UGRAMM->trace("[Locking] hNames[{}] {} :: Lock gNames {} :: GID{} :: verify gNames {}", y, hNames[y], (*hConfig)[y].LockGNode, suitableGIDs[i], gNames[suitableGIDs[i]]);
      }
    }
    
    for (int i = 0; i < suitableGIDs.size(); i++)
    { // first route the signal on the output of y

      int GID = suitableGIDs[i];
      // Confriming the Vertex correct type:
      if (!compatibilityCheck(GID, y, hConfig, gConfig))
      {
        compatibilityStatus = false;
        continue;
      }

      //------------------ Routing Setup ---------------------//
      ripUpRouting(y, G);         //Ripup the previous routing
      (*Users)[GID].push_back(y);   //Users update                 
      invUsers[y] = GID;            //InvUsers update
      //------------------------------------------------------//

      // Cost and history costs are calculated for the FuncCell:
      totalCosts[GID] += calculate_cost(GID);
      
      //Placement of the signal Y:
      totalCosts[GID] += routeSignal(G, H, y, gConfig, hConfig);

      //-------- Debugging statements ------------//
      if (UGRAMM->level() <= spdlog::level::trace)
        printRouting(y);
        UGRAMM->debug("For application node {} :: routing for location [{}] has cost {}", hNames[y], gNames[GID], totalCosts[GID]);
      //------------------------------------------//

      //Early exit if the cost is greater than bestCost:
      if (totalCosts[GID] > bestCost)
        continue;

      //----------------------------------------------
      // now route the signals on the input of y
      //----------------------------------------------

      boost::tie(ei, ei_end) = in_edges(yD, *H);
      for (; (ei != ei_end); ei++)
      {
        int driverHNode = source(*ei, *H);

        if (invUsers[driverHNode] == NOT_PLACED)
          continue; // driver is not placed
        
        int driverGNode = invUsers[driverHNode];

        //------------------ Routing Setup ---------------------//
        ripUpRouting(driverHNode, G);
        (*Users)[driverGNode].push_back(driverHNode); //Users update
        invUsers[driverHNode] = driverGNode;          //InvUsers update
        //------------------------------------------------------//

        // Newfeature: rip up from load: ripUpLoad(G, driver, outputPin);
        totalCosts[GID] += routeSignal(G, H, driverHNode, gConfig, hConfig);

        UGRAMM->debug("Routing the signals on the input of {}", hNames[y]);
        UGRAMM->debug("For {} -> {} :: {} -> {} has cost {} :: best cost {}", hNames[driverHNode], hNames[y], gNames[driverGNode], gNames[GID], totalCosts[GID], bestCost);

        if (totalCosts[GID] > bestCost)
          break;
      }

      UGRAMM->debug("Total cost for {} is {} :: best cost {}", hNames[y], totalCosts[GID], bestCost);

      if (totalCosts[GID] >= MAX_DIST)
        continue;

      if (totalCosts[GID] < bestCost)
      {
        bestIndexFuncCell = GID;
        bestCost = totalCosts[GID];
        compatibilityStatus = true;
      }
      else if (totalCosts[GID] == bestCost)
      {
        if (!(rand() % 2))
        {
          bestIndexFuncCell = GID;
        }
        compatibilityStatus = true;
      }
    }
  }

  if (!lockingNodeStatus){
    for (int i = 0; i < num_vertices(*G); i++) // don't care for locking
    { // first route the signal on the output of y

      // Confriming the Vertex correct type:
      if (!compatibilityCheck(i, y, hConfig, gConfig))
      {
        compatibilityStatus = false;
        continue;
      }

      // only the vertex type is checked, not the connectivity to the functional primitive/another already mapped vertex

      // Skip Fully Locked Nodes
      if (skipFullyLockedNodes){
        if ((*gConfig)[i].gLocked){
          UGRAMM->trace("Skipping locked device model node {} for mapping application graph node {} ", gNames[i], hNames[y]);
          continue;
        }
      }

      //------------------ Routing Setup ---------------------//
      ripUpRouting(y, G);         //Ripup the previous routing
      UGRAMM->debug("[RIP] Routing ripped for y {}, will update with gnode i {}", hNames[y], gNames[i]);
      (*Users)[i].push_back(y);   //Users update                 
      invUsers[y] = i;            //InvUsers update
      //------------------------------------------------------//

      // Cost and history costs are calculated for the FuncCell:
      totalCosts[i] += calculate_cost(i);
      UGRAMM->debug("For application node {} :: location [{}] has VERTEX cost {}", hNames[y], gNames[i], totalCosts[i]);
      //Placement of the signal Y:
      UGRAMM->debug("[findMinVertexModel] Calling routeSignal for placement of y {}", hNames[y]);
      totalCosts[i] += routeSignal(G, H, y, gConfig, hConfig);

      //-------- Debugging statements ------------//
      if (UGRAMM->level() <= spdlog::level::trace)
        printRouting(y);
      UGRAMM->debug("[Placement] For application node {} :: routing for location [{}] has cost {}", hNames[y], gNames[i], totalCosts[i]);
      //------------------------------------------//

      //Early exit if the cost is greater than bestCost:
      if (totalCosts[i] > bestCost)
        continue;

      //----------------------------------------------
      // now route the signals on the input of y
      //----------------------------------------------

      // here we route the functional primitive when y is one of the OUTPUT prims
      boost::tie(ei, ei_end) = in_edges(yD, *H);
      for (; (ei != ei_end); ei++)
      {
        int driverHNode = source(*ei, *H);

        UGRAMM->debug("driverHNode is {}", hNames[driverHNode]);
        if (invUsers[driverHNode] == NOT_PLACED)
          continue; // driver is not placed
        
        int driverGNode = invUsers[driverHNode];

        //------------------ Routing Setup ---------------------//
        ripUpRouting(driverHNode, G);
        (*Users)[driverGNode].push_back(driverHNode); //Users update
        invUsers[driverHNode] = driverGNode;          //InvUsers update
        //------------------------------------------------------//

        // Newfeature: rip up from load: ripUpLoad(G, driver, outputPin);
        UGRAMM->debug("[findMinVertexModel] Calling routeSignal for placement of driverHNode {}", hNames[driverHNode]);
        totalCosts[i] += routeSignal(G, H, driverHNode, gConfig, hConfig);

        UGRAMM->debug("Routing the signals on the input of {}", hNames[y]);
        UGRAMM->debug("For {} -> {} :: {} -> {} has cost {}", hNames[driverHNode], hNames[y], gNames[driverGNode], gNames[i], totalCosts[i]);

        if (totalCosts[i] > bestCost)
          UGRAMM->debug("ROUTING FOR INPUT SIGNALS INTERRUPTED");
          break;
      }

      // if y and all its inputsignals have been mapped to some node with OK cost
      UGRAMM->debug("Total cost for {} considering i = {} is {}\n", hNames[y], gNames[i], totalCosts[i]);

      if (totalCosts[i] >= MAX_DIST)
        continue;

      if (totalCosts[i] < bestCost)
      {
        bestIndexFuncCell = i;
        bestCost = totalCosts[i];
        compatibilityStatus = true;
      }
      else if (totalCosts[i] == bestCost)
      {
        if (!(rand() % 2))
        {
          bestIndexFuncCell = i;
        }
        compatibilityStatus = true;
      }
    }
  }

  if (bestIndexFuncCell == -1)
  {
    UGRAMM->error("FATAL ERROR -- COULD NOT FIND A VERTEX MODEL FOR VERTEX {} {}", y, hNames[y]);
    if (!compatibilityStatus)
      UGRAMM->error("Nodes in the device model does not supports {} Opcode", (*hConfig)[y].Opcode);
    exit(-1);
  }

  UGRAMM->debug("The bertIndex found for {} from the device-model is {} with cost of {}", hNames[y], gNames[bestIndexFuncCell], bestCost);

  // Final rig-up before doing final routing:

  //------------- Final Routing Setup ---------------------//
  ripUpRouting(y, G);         //Ripup the previous routing
  invUsers[y] = bestIndexFuncCell;           //InvUsers update
  (*Users)[bestIndexFuncCell].push_back(y);  //Users update                 
  //------------------------------------------------------//

  // Final-placement for node 'y':
  UGRAMM->debug("[findMinVertexModel] Calling routeSignal for !!!FINAL!!! placement of y {}", hNames[y]);
  routeSignal(G, H, y, gConfig, hConfig);

  // Final routing all of the inputs of the node 'y':
  boost::tie(ei, ei_end) = in_edges(yD, *H);
  for (; ei != ei_end; ei++)
  {
    int driverHNode = source(*ei, *H);

    if (invUsers[driverHNode] == NOT_PLACED)
      continue; // driver is not placed

    int driverGNode = invUsers[driverHNode];

    //------------------ Routing Setup ---------------------//
    ripUpRouting(driverHNode, G);
    (*Users)[driverGNode].push_back(driverHNode); //Users update
    invUsers[driverHNode] = driverGNode;          //InvUsers update
    //------------------------------------------------------//

    UGRAMM->debug("[findMinVertexModel] Calling routeSignal for !!!FINAL!!! placement of driverHNode {}", hNames[driverHNode]);
    routeSignal(G, H, driverHNode, gConfig, hConfig);
  }

  return 0;
}

//---------------------------------------------------------------------//

/**
 * Finds a minor embedding of graph H into graph G.
 */
int findMinorEmbedding(DirectedGraph *H, DirectedGraph *G, std::map<int, NodeConfig> *hConfig, std::map<int, NodeConfig> *gConfig)
{

  int ordering[num_vertices(*H)];
  for (int i = 0; i < num_vertices(*H); i++)
  {
    ordering[i] = i;
    UGRAMM->trace("Ordering[{}]: {} ", i, ordering[i]);
  }

  bool done = false;
  bool success = false;
  iterCount = 0;

  explored.reset();
  float frac;

  while (!done)
  {

    iterCount++;
    UGRAMM->debug("\n\n");
    UGRAMM->info("***** BEGINNING OUTER WHILE LOOP ***** ITER {}", iterCount);

    // Not using randomize sorting:
    //randomizeList(ordering, num_vertices(*H));
    
    // Sorting the nodes of H according to the size (number of vertices) of their vertex model
    sortList(ordering, num_vertices(*H), hConfig);
    if (UGRAMM->level() <= spdlog::level::debug){
      for (int i = 0; i < num_vertices(*H); i++){
        UGRAMM->debug("Afer sortlist (sort) Interation {} | Ordering[{}]: {} | hNames[{}]: {}", iterCount, i, ordering[i], ordering[i], hNames[ordering[i]]);
      }
    }

    for (int k = 0; k < num_vertices(*H); k++)
    {
      int y = ordering[k];
      
      if(hNames[y] == "NULL")
        continue;
      
      UGRAMM->debug("\n");
      UGRAMM->debug("--------------------- New Vertices Mapping Start ---------------------------");
      UGRAMM->debug("Finding vertex model for: {} with Current vertex-size: {}", hNames[y], (*Trees)[y].nodes.size());

      findMinVertexModel(G, H, y, hConfig, gConfig);
    } // for k

    int TO = totalOveruse(G);

    if (iterCount >= 1)
    {
      frac = (float)TO / totalUsed(G);
      UGRAMM->info("FRACTION OVERLAP: {}", frac);
      if (TO == 0)
      {
        UGRAMM->info("UGRAMM_PASSED :: [iterCount] :: {} :: [frac] :: {} :: [num_vertices(H)] :: {}", iterCount, frac, num_vertices(*H));
        done = true;
        success = true;
      }
    }

    adjustHistoryCosts(G);

    PFac *= pfac_mul;    // adjust present congestion penalty

    if (iterCount > (max_iter-1)) // limit the iteration count to ~40 iterations!
    {
      UGRAMM->error("UGRAMM_FAILURE. OVERUSED: {} USED: {}", TO, totalUsed(G));
      done = true;
    }

    if (success)
    {
      return 1;
    }

  } // while !done

  return 0;
}

//---------------------------------------------------------------------//

/**
 * Main function entry point for the UGRAMM - program.
 */
int main(int argc, char **argv)
{

  //--------------- Starting timestamp -------------------------
  /* get start timestamp */
  struct timeval totalTime;
  gettimeofday(&totalTime, NULL);
  uint64_t startTotal = totalTime.tv_sec * (uint64_t)1000000 + totalTime.tv_usec;
  //------------------------------------------------------------

  //--------------------------------------------------------------------//
  //----------------- Setting up required variables --------------------//
  //--------------------------------------------------------------------//

  DirectedGraph H, G;
  boost::dynamic_properties dp(boost::ignore_other_properties);

  //----------------- For [H] --> Application Graph --------------------//
  dp.property("label", boost::get(&DotVertex::H_Name, H));                 //--> [Required] Contains name of the operation in Application graph (ex: Load_0)
  dp.property("opcode", boost::get(&DotVertex::H_Opcode, H));              //--> [Required] Contains the Opcode of the operation (ex: op, const, input and output)
  dp.property("load", boost::get(&EdgeProperty::H_LoadPin, H));            //--> [Required] Edge property describing the loadPin to use for the edge
  dp.property("driver", boost::get(&EdgeProperty::H_DriverPin, H));        //--> [Required] Edge property describing the driverPin to use for the edge
  dp.property("latency", boost::get(&DotVertex::H_Latency, H));            //--> [Optional] Contains for latency of the node --> check this as it needs to be between the edges
  dp.property("h_width", boost::get(&DotVertex::H_Width, H));              //--> [Optional] Width of the application-node

  //---------------- For [G] --> Device Model Graph --------------------//
  dp.property("G_Name", boost::get(&DotVertex::G_Name, G));               //--> [Required] Contains the unique name of the cell in the device model graph.
  dp.property("G_CellType", boost::get(&DotVertex::G_CellType, G));       //--> [Required] Contains the Opcode of the CellType (FuncCell, RouteCell, PinCell)
  dp.property("G_NodeType", boost::get(&DotVertex::G_NodeType, G));       //--> [Required] Contains the NodeType of Device Model Graph (For example "ALU" for CellType "FuncCell") 
  dp.property("G_VisualX", boost::get(&DotVertex::G_VisualX, G));         //--> [Optional] X location for only visualization purpose.
  dp.property("G_VisualY", boost::get(&DotVertex::G_VisualY, G));         //--> [Optional] Y location for only visualization purpose.
  dp.property("G_Width", boost::get(&DotVertex::G_Width, G));             //--> [Optional] Width of the hardware-node.

  // gConfig and hConfig contains the configuration information about the particular node.
  std::map<int, NodeConfig> gConfig, hConfig;

  //--------------------------------------------------------------------//
  //---------------------- Command line arguments ----------------------//
  //--------------------------------------------------------------------//

  po::variables_map vm; // Value map

  // Input variables:
  std::string deviceModelFile; // Device Model graph file
  std::string applicationFile; // Application Model graph file
  std::string configFile;      // Config file
  int seed_value;              // Seed number
  int verbose_level = 0;           // Verbosity level => [0: info], [1: debug], [2: trace]
  int drc_verbose_level = 0;   // DRC Verbosity level => [0: err], [1: warn], [2: info], [3: debug]
  bool drc_disable = false;    // drc disable => default to false

  po::options_description desc("[UGRAMM] allowed options =");
  desc.add_options()
      ("help,h", "Print help messages")
      ("seed", po::value<int>(&seed_value), "Seed for the run [optional]")
      ("verbose_level", po::value<int>(&verbose_level), "0: info [Default], 1: debug, 2: trace [optional]")
      ("dfile", po::value<std::string>(&deviceModelFile)->required(), "Device model file [required]")
      ("afile", po::value<std::string>(&applicationFile)->required(), "Application graph file [required]")
      ("config", po::value<std::string>(&configFile), "UGRAMM config file [optional]")
      ("drc_disable", po::bool_switch(&drc_disable), "disable DRC [optional]")
      ("max_iter", po::value<int>(&max_iter), "Maximum number of iterations UGRAMM will run [optional; defaults to 40]" )
      ("pfac_mul", po::value<float>(&pfac_mul), "Multiplier for present congestion cost [optional; defaults to 1.1]")
      ("hfac_mul", po::value<float>(&hfac_mul), "Multiplier for history congestion cost [optional; defaults to 1]")
      ("base_cost", po::value<float>(&base_cost), "Base cost of using a wire-segment in Pathfinder [optional; defaults to 1]")
      ("drc_verbose_level", po::value<int>(&drc_verbose_level), "0: err [Default], 1: warn, 2: info, 3: debug [optional]");

  po::store(po::parse_command_line(argc, argv, desc), vm);

  if (vm.count("help") || (argc == 1)) //Even if no-arguments provided, print the description for UGRAMM.
  {
    std::cout << desc << std::endl;
    return 0;
  }

  po::notify(vm);

  //---------------------- setting up the input variables --------------//

  if (verbose_level == 0)
    UGRAMM->set_level(spdlog::level::info);  // Set global log level to debug
  else if (verbose_level == 1)
    UGRAMM->set_level(spdlog::level::debug); // Set global log level to debug
  else if (verbose_level == 2)
    UGRAMM->set_level(spdlog::level::trace); // Set global log level to debug

  // Seed setup:
  srand(seed_value);

  //Hfac setup:
  HFac = hfac_mul ;   // adjust history of congestion penalty

  // -------------------- Printing out the config received -------------//
  UGRAMM->info("[CONFIG] seed value set to {}", seed_value);
  UGRAMM->info("[CONFIG] verbose_level set to {}", verbose_level);
  UGRAMM->info("[CONFIG] dfile set to {}", deviceModelFile);
  UGRAMM->info("[CONFIG] afile set to {}", applicationFile);
  UGRAMM->info("[CONFIG] max_iter set to {}", max_iter);
  UGRAMM->info("[CONFIG] pfac_mul set to {}", pfac_mul);
  UGRAMM->info("[CONFIG] hfac_mul set to {}", hfac_mul);
  UGRAMM->info("[CONFIG] drc is {} with verboisty level {}", drc_disable, drc_verbose_level);

  // Config file parsing:
  if (!configFile.empty())
  {
    UGRAMM->info("[CONFIG] config file set to {}", configFile);
    std::ifstream f(configFile);
    jsonParsed = json::parse(f);                            // Parsing the Json file.
    UGRAMM->info("Parsed JSON file {} ", jsonParsed.dump()); // Printing JSON file for the info purpose.
    jsonUppercase(jsonParsed);                              // Normalizing the JSON to uppercase letters.
    UGRAMM->info("Normalized JSON {} ", jsonParsed.dump()); // Printing JSON file for the info purpose.
  }

  //--------------------------------------------------------------------//
  //----------------- STEP 0 : READING DEVICE MODEL --------------------//
  //--------------------------------------------------------------------//

  std::ifstream dFile;                                      // Defining the input file stream for device model dot file
  dFile.open(deviceModelFile);                              // Passing the device_Model_dot file as an argument!
  readDeviceModelPragma(dFile, UgrammPragmaConfig);         // Reading the device model pragma from the device-model dot file.
  boost::read_graphviz(dFile, G, dp);                       // Reading the dot file
  readDeviceModel(&G, &gConfig);                            // Reading the device model file.

  //--------------------------------------------------------------------//
  //----------------- STEP 1: READING APPLICATION DOT FILE -------------//
  //--------------------------------------------------------------------//

  // read the DFG from a file
  std::ifstream iFile;                                      // Defining the input file stream for application_dot file
  iFile.open(applicationFile);                              // Passing the application_dot file as an argument!
  readApplicationGraphPragma(iFile, UgrammPragmaConfig);    // Reading the application-graph pragma from the device-model dot file.
  boost::read_graphviz(iFile, H, dp);                       // Reading the dot file
  readApplicationGraph(&H, &hConfig, &gConfig);             // Reading the Application graph file.

  //--------------------------------------------------------------------//
  //----------------- STEP 2: DRC Verification -------------------------//
  //--------------------------------------------------------------------//
  double secondsDRC;
  if (!drc_disable){
    secondsDRC = runDRC(&H, &G, &hConfig, &gConfig, drc_verbose_level);
  }

  //--------------------------------------------------------------------//
  //--------- STEP 3: Initializing the mapping-datastructures ----------//
  //--------------------------------------------------------------------//

  Trees = new std::vector<RoutingTree>(num_vertices(H));    // routing trees for every node in H
  Users = new std::vector<std::list<int>>(num_vertices(G)); // for every node in device model G track its users
  HistoryCosts = new std::vector<int>(num_vertices(G));     // for history of congestion in PathFinder
  TraceBack = new std::vector<int>(num_vertices(G));

  for (int i = 0; i < num_vertices(G); i++)
  {
    (*Users)[i].clear();
    (*HistoryCosts)[i] = 0;
    (*TraceBack)[i] = -1;
  }

  for (int i = 0; i < num_vertices(H); i++)
  {
    (*Trees)[i].parent.clear();
    (*Trees)[i].children.clear();
    (*Trees)[i].nodes.clear();
    invUsers[i] = -1;
  }

  //--------------------------------------------------------------------//
  //----------------- STEP 4: Finding minor embedding ------------------//
  //--------------------------------------------------------------------//

  //--------------- Starting timestamp -------------------------
  /* get start timestamp */
  struct timeval grammTime;
  gettimeofday(&grammTime, NULL);
  uint64_t startGramm = grammTime.tv_sec * (uint64_t)1000000 + grammTime.tv_usec;
  //------------------------------------------------------------

  int success = findMinorEmbedding(&H, &G, &hConfig, &gConfig);

  for (int i = 0; i < num_vertices(G); i++)
  {
    if (gNames[i].find("CNTRLMERGEN2") != std::string::npos) {
      UGRAMM->debug("(USER CHECK) node {} has users", gNames[i]);
      for (int n : (*Users)[i]) {
        UGRAMM->debug("(USER CHECK) node {}", gNames[n]);
      }
    }
  }

  if (success)
  {
    // Printing vertex model:
    printVertexModels(&H, &G, &hConfig, invUsers);

    // Visualizing mapping result in neato:
    printMappedResults(&H, &G, &hConfig, &gConfig, UgrammPragmaConfig);

    UGRAMM->info("[RESULT] ----------- UGRAMM_PASSED -----------");
  }
  else 
  {
    UGRAMM->error("[RESULT] UGRAMM_FAILED");
  }

  //--------------- get elapsed time -------------------------
  if (!drc_disable){
    UGRAMM->info("Total time taken for [DRC] :: {} Seconds", secondsDRC);
  }
  //------------------------------------------------------------

  //--------------- get elapsed time -------------------------
  gettimeofday(&grammTime, NULL);
  uint64_t endGramm = grammTime.tv_sec * (uint64_t)1000000 + grammTime.tv_usec;
  uint64_t elapsedGramm = endGramm - startGramm;
  double secondsGramm = static_cast<double>(elapsedGramm) / 1000000.0;
  UGRAMM->info("[RESULT] Total time taken for [mapping] :: {} Seconds", secondsGramm);
  //------------------------------------------------------------

  //--------------- get elapsed time -------------------------
  gettimeofday(&totalTime, NULL);
  uint64_t endTotal = totalTime.tv_sec * (uint64_t)1000000 + totalTime.tv_usec;
  uint64_t elapsedTotal = endTotal - startTotal;
  double secondsTotal = static_cast<double>(elapsedTotal) / 1000000.0;
  UGRAMM->info("[RESULT] Total time taken for [UGRAMM]:: {} Seconds", secondsTotal);
  //------------------------------------------------------------

  //---- Experimentation:
  float used_routing_resources = 0;
  float total_routing_resources = 0;
  
  float used_functional_resources = 0;
  float total_functional_resources = 0;

  for (int i = 0; i < num_vertices(G); i++)
  {
    if(gConfig[i].Cell == "ROUTECELL")
    {
      if((*Users)[i].size() >= 1)
        used_routing_resources += 1;
      
      total_routing_resources += 1; 
    }

    if(gConfig[i].Cell == "FUNCCELL")
    {
      if((*Users)[i].size() >= 1)
        used_functional_resources += 1;
       
      total_functional_resources += 1; 
    }
  }
  float RRUSED = (used_routing_resources/total_routing_resources) * 100;
  float FFUSED = (used_functional_resources/total_functional_resources) * 100;
 
  UGRAMM->info("[RESULT] Routing resources percentage usage : {} % ({}/{})",RRUSED,used_routing_resources,total_routing_resources);
  UGRAMM->info("[RESULT] Functional resources percentage usage : {} % ({}/{})",FFUSED,used_functional_resources,total_functional_resources); 
  //--------

  return 0;
}
