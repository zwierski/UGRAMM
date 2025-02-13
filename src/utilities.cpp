//=======================================================================//
// Universal GRAaph Minor Mapping (UGRAMM) method for CGRA               //
// file : utilities.cp                                                   //
// description: contains functions needed for printing, visualization    //
//              pragma etc.                                              //
//=======================================================================//

#include "../lib/UGRAMM.h"
#include "../lib/utilities.h"
#include "../lib/routing.h"

std::vector<std::string> colors = {
    "#FFA500", // Orange (Replaced Light Pink)
    "#FFFFE0", // Light Yellow
    "#D8BFD8", // Thistle (Lighter Purple)
    "#D2B48C", // Tan (Lighter Brown)
    "#AFEEEE", // Light Turquoise
    "#E0FFFF", // Light Cyan
    "#FFDAB9", // Peach Puff (Lighter Orange)
    "#00FF00", // Lime (Replaced Pink)
    "#AFEEEE", // Light Turquoise
    "#DDA0DD"  // Plum (Lighter Indigo)
}; // Color code for different FunCells

std::string input_pin_color = "#ADD8E6";           // Pre-defined color-code for the input-pin cell ( Light Blue)
std::string output_pin_color = "#FFB6C1";          // Pre-defined color-code for the output-pin cell (Lighter Red/Pink)
std::string unused_cell_color = "#A9A9A9";         // Pre-defined color-code for the unused cell
std::map<std::string, std::string> funCellMapping; // Key-> Device-model node-name :: Value-> Mapped application name.

//------------------------------------------------------------------------------------//
//------------------- [Utilities] String manipulation operations ---------------------//
//------------------------------------------------------------------------------------//

/**
 * Removes the curly brackets '{' and '}' from the given string.
 * The kernels present in CGRA-ME have {} included in the node names, which hinders dot visualization.
 */
std::string removeCurlyBrackets(const std::string &input)
{
  std::string result = input;

  // Find and remove the opening curly bracket
  size_t pos = result.find('{');
  if (pos != std::string::npos)
  {
    result.erase(pos, 1);
  }

  // Find and remove the closing curly bracket
  pos = result.find('}');
  if (pos != std::string::npos)
  {
    result.erase(pos, 1);
  }

  return result;
}

/**
 * Changes the delimiters in the given string from "." to "_" (neato dot format does not support "." delimiter in the node name)
 */
std::string gNames_deliemter_changes(std::string gNames)
{
  std::string modified_string;

  // Iterate through each character in the input string
  for (char c : gNames)
  {
    if (c == '.')
    {
      // Replace '.' with '_'
      modified_string += '_';
    }
    else
    {
      // Keep the character as it is
      modified_string += c;
    }
  }

  return modified_string;
}

/**
 * Removes a specified substring from the original string.
 */
std::string string_remover(std::string original_string, std::string toRemove)
{
  std::string modified_string;

  // Getting the position of the sub_string which needs to be removed:
  size_t pos = original_string.find(toRemove);

  if (pos != std::string::npos)
  {
    modified_string = original_string.substr(0, pos);
  }
  else
  {
    modified_string = original_string;
  }

  return modified_string;
}

/**
 * Converts all keys and values of the parsed JSON object to uppercase for normalization.
 */
void jsonUppercase(json &j)
{
  json result;

  for (auto &[key, value] : j.items())
  {
    std::string upperKey = boost::to_upper_copy(key);
    auto upperValue = value;
    json upperArray = json::array();
    if (value.is_array())
    { // Case1: value is an array.
      for (auto &el : upperValue)
      {
        std::string upperValue = el.get<std::string>();
        std::transform(upperValue.begin(), upperValue.end(), upperValue.begin(), ::toupper);
        upperArray.push_back(upperValue);
      }
    }
    result[upperKey] = upperArray;
  }
  j = result;
}

//------------------------------------------------------------------------------------//
//---------------------- [Utilities] PRAGMA related functions ------------------------//
//------------------------------------------------------------------------------------//

/**
 * Reads a multi-line comment containing PRAGMA from the given file.
 */
std::string readCommentSection(std::ifstream &inputFile)
{
  std::string line;
  std::string commentSection = {};
  bool comment_started = false;

  while (std::getline(inputFile, line))
  {
    size_t startPos = line.find("/*");
    if (startPos != std::string::npos)
    {
      comment_started = true;
    }
    else if (comment_started == true)
    {
      size_t endPos = line.find("*/");
      if (endPos != std::string::npos)
      {
        return commentSection;
      }
      else
      {
        std::string upperCaseline = boost::to_upper_copy(line);
        commentSection += upperCaseline + "\n";
      }
    }
  }
  return commentSection;
}

/**
 * Checks whether placement is required for the given opcode based on the information given in the JSON.
 * In JSON, we can either have opcode("Reg") or nodeType("ALU/Memport") to be skipped.
*/ 
bool skipPlacement(std::string hOpcode, json &jsonParsed)
{
  if (jsonParsed["SKIP-PLACEMENT"].empty()){
    return false;
  }
  else
  {
    // First case: Opcode itself is mentioned in the json file to be skipped.
    if (std::find(jsonParsed["SKIP-PLACEMENT"].begin(), jsonParsed["SKIP-PLACEMENT"].end(), hOpcode) != jsonParsed["SKIP-PLACEMENT"].end())
    { 
      UGRAMM->trace(" [Case-1] : Skipping found Opcode {} in JSON",  hOpcode);
      return true;
    }

    // Second case: NodeType to be skipped is given in the JSON file:
    std::vector<std::string> gTypeSupported;

    for (auto& [key, value] : UgrammPragmaConfig.items()) {
      if (value.is_array()) {
        if (std::find(UgrammPragmaConfig[key].begin(), UgrammPragmaConfig[key].end(), hOpcode) != UgrammPragmaConfig[key].end())
          gTypeSupported.push_back(key);
      }
    }

    for (const auto &value : gTypeSupported)
    { 
      if (std::find(jsonParsed["SKIP-PLACEMENT"].begin(), jsonParsed["SKIP-PLACEMENT"].end(), value) != jsonParsed["SKIP-PLACEMENT"].end())
      {
        UGRAMM->trace(" [Case-2] : Skipping found NodeType {} in JSON",  value);
        return true;
      }
    }
    return false;
  }
}


/**
 * Checks whether locking is required for the given hNamed based on the information given in the JSON.
 * In JSON, we define the locking code as "hName::gName"
*/ 
bool needLocking(int HID, json &jsonParsed, std::string& jsonLockNode)
{
  if (jsonParsed["LOCK-NODES"].empty()){
    return false;
  } else {

    return std::any_of(jsonParsed["LOCK-NODES"].begin(), jsonParsed["LOCK-NODES"].end(), [&](const std::string& lockGNode) {
        // Capitalize the current element for comparison
        std::string lockGNodeCap = boost::to_upper_copy(lockGNode);
        // Check if the element is found
        if (lockGNodeCap.find(hNames[HID]) != std::string::npos){
          UGRAMM->trace(" [Locking] : Found hNames {} is substring {}",  hNames[HID], lockGNodeCap);
          jsonLockNode = lockGNodeCap;
          return true;
        } else {
          return false;
        }
    });
  }
}

/**
 * Reads, checks, and stores PRAGMA directives from the device model file.
*/
void readDeviceModelPragma(std::ifstream &deviceModelFile, json &UgrammPragmaConfig)
{ 
  // Step 1: Parse the comment section into string.
  std::string commentSection = readCommentSection(deviceModelFile);
  UGRAMM->trace(" Device model pragma READ from the dot file :: {}", commentSection);

  // Step 2: Parse the JSON file from the comment Section
  UgrammPragmaConfig = json::parse(commentSection);

  // Step 3: Print all keys and values
  for (auto& [key, value] : UgrammPragmaConfig.items()) {
      std::cout << key << " : ";
      if (value.is_array()) {
          std::cout << "[ ";
          for (const auto& item : value) {
              std::cout << item << " ";
          }
          std::cout << "]";
      } else {
          std::cout << value;
      }
      std::cout << std::endl;
  }
}

/**
 * Reads, checks, and stores PRAGMA directives from the application graph file.
*/ 
void readApplicationGraphPragma(std::ifstream &applicationGraphFile, json &UgrammPragmaConfig)
{ 
  // Step 1: Parse the comment section into string.
  std::string commentSection = readCommentSection(applicationGraphFile);

  // Step 2: Parse the JSON file from the comment Section
  json applicationGraphPragma = json::parse(commentSection);

  // Step 3: Check the content of applicationGraph matches with Device model or not.
  for (auto& [key, value] : applicationGraphPragma.items()) {
    if (value.is_array()) {
      for (const auto& item : value) {
        if (std::find(UgrammPragmaConfig[key].begin(), UgrammPragmaConfig[key].end(), item) == UgrammPragmaConfig[key].end()) {
          // Item not found, so not a subset
          UGRAMM->error("[H -- Compatibility check] Requested Opcode by H {} not found in supported operations of device model for {} failed", item.dump(), key);
          exit(-1);
        }
      }
      UGRAMM->info("[H] Compatibility of pragma for {} passed", key);
    }
    else {
      // Convert `value` to string for logging
      std::string valueStr = value.dump();
      UGRAMM->info("[H] Key -> {}, Value -> {} added into UGRAMM Pragma Config", key, valueStr);

      // Add key-value pair to UgrammPragmaConfig
      UgrammPragmaConfig[key] = value;
    }
  }
}

/**
 * Checks if the application graph's node width is less than or equal to the device model's node width.
 */
bool widthCheck(int hWidth, int gWidth) 
{
  //Not doing widthCheck if any of the widths are not set or "0"
  if ((gWidth == 0) || (hWidth == 0)) 
  {
    return true;
  }

  //Only doing width check when both of the arguments are available, if incase user has not provided width for any block then it is considered as '0' for which we don't do width check:
  if(hWidth <= gWidth)
  {
    UGRAMM->trace("The width required by application-graph [{}] is supported by device-model graph[{}]", hWidth, gWidth);
    return true;
  }
  else {
    return false;
  }
}

/*
 * Checks whether the current opcode required by the application node is supported by the device model node.
 * 
 * This function determines if the opcode needed by the application node (represented by `hOpcode`)
 * is compatible with or supported by the device model node type (represented by `gType`).
*/
bool compatibilityCheck(int gID, int hID, std::map<int, NodeConfig> *hConfig, std::map<int, NodeConfig> *gConfig)
{ 
  std::string gType   = (*gConfig)[gID].Type;
  std::string hOpcode = (*hConfig)[hID].Opcode;

  // For nodes such as RouteCell, PinCell the UgrammPragmaConfig array will be empty.
  // Pragma array's are only parsed for the FuncCell types.
  if (UgrammPragmaConfig[gType].size() == 0)
    return false;

  // Finding Opcode required by the application graph supported by the device model or not.
  for (const auto& value : UgrammPragmaConfig[gType])
  {
    if (value == hOpcode)
    {
      UGRAMM->trace("{} node from device model supports {} Opcode", gType, hOpcode);
      return widthCheck((*hConfig)[hID].width, (*gConfig)[gID].width);
    }
  }

  return false;
}

//------------------------------------------------------------------------------------//
//---------------------- [Utilities] Printing and visualization ----------------------//
//------------------------------------------------------------------------------------//

/**
 * Prints routing information to a mapping-output file.
 */ 
void printRoutingResults(int y, std::ofstream &positionedOutputFile, std::ofstream &unpositionedOutputFile, std::map<int, NodeConfig> *hConfig, std::map<int, NodeConfig> *gConfig)
{

  struct RoutingTree *RT = &((*Trees)[y]);

  if (RT->nodes.size() == 0)
    return;

  int n = RT->nodes.front();
  int orign = n;

  std::list<int>::iterator it = RT->nodes.begin();

  for (; it != RT->nodes.end(); it++)
  {
    int m = *it;

    if (m == orign)
      continue;

    //if (boost::algorithm::contains(gNames[RT->parent[m]], "OUTPIN"))
    //
    //PIN Cell should be defined as OUT/IN in the alias of the device-model and applicationGraph 
    if((*gConfig)[RT->parent[m]].Type == "OUT")
    {
      //  This else if loop is hit when the source is outPin and the while loop below traces the connection from the outPin and exits when found valid inPin.
      //  ex: outPin -> switchblock -> switchblock_pe_input -> pe_inPin
      //      This loop will trace the above connection and show a connection between outPin -> pe_inPin
      int current_sink = *it;
      while (it != RT->nodes.end())
      {
        //if (boost::algorithm::contains(gNames[current_sink], "INPIN"))
        if((*gConfig)[current_sink].Type == "IN")
        {
          positionedOutputFile << gNames_deliemter_changes(gNames[RT->parent[m]]) << " -> " << gNames_deliemter_changes(gNames[current_sink]) << "\n";
          unpositionedOutputFile << gNames_deliemter_changes(gNames[RT->parent[m]]) << " -> " << gNames_deliemter_changes(gNames[current_sink]) << "\n";
        }
        it++;
        current_sink = *it;
      }
      break; // As the above while loop traces till the end of the connection, we have break out of the for loop iterator!!
    }
  }
}

/**
 * Connects associated pins to the specified FunCell in the device model graph.
*/ 
void mandatoryFunCellConnections(int gNumber, std::string FunCellName, DirectedGraph *G, std::ofstream &positionedOutputFile, std::ofstream &unpositionedOutputFile)
{

  vertex_descriptor yD = vertex(gNumber, *G); // Vertex Descriptor for FunCell

  //--------------------------
  // Output pin-connection:
  //--------------------------
  out_edge_iterator eo, eo_end;
  boost::tie(eo, eo_end) = out_edges(yD, *G);
  for (; eo != eo_end; eo++)
  { 
    int selectedCellOutputPin = target(*eo, *G);

    // this function also assumes there's only 1 output pin. added the forloop to account for multiple outputpins for one prim
    UGRAMM->debug("funcell {} has outputpin {}", gNames[gNumber], gNames[selectedCellOutputPin]);

    //if (((*Users)[selectedCellOutputPin].size() >= 1))
    //{
      positionedOutputFile << FunCellName + "_" + funCellMapping[gNames[gNumber]] << " -> " << gNames_deliemter_changes(gNames[selectedCellOutputPin]) << "\n";
      unpositionedOutputFile << FunCellName + "_" + funCellMapping[gNames[gNumber]] << " -> " << gNames_deliemter_changes(gNames[selectedCellOutputPin]) << "\n";
      UGRAMM->debug("writing edge {}_{} -> {} to dotfile", FunCellName, funCellMapping[gNames[gNumber]],  gNames_deliemter_changes(gNames[selectedCellOutputPin]));
    //}
  }

  //--------------------------
  // Input Pin-connection:
  //--------------------------
  in_edge_iterator ei, ei_end;
  boost::tie(ei, ei_end) = in_edges(yD, *G);
  for (; ei != ei_end; ei++)
  {
    int source_id = source(*ei, *G);
    if (((*Users)[source_id].size() >= 1))
    {
      positionedOutputFile << gNames_deliemter_changes(gNames[source_id]) << " -> " << FunCellName + "_" + funCellMapping[gNames[gNumber]] << "\n";
      unpositionedOutputFile << gNames_deliemter_changes(gNames[source_id]) << " -> " << FunCellName + "_" + funCellMapping[gNames[gNumber]] << "\n";
      //UGRAMM->debug("writing edge {} -> {}_{} to dotfile", gNames_deliemter_changes(gNames[selectedCellOutputPin]), FunCellName, funCellMapping[gNames[gNumber]]);
    }
  }
}

/**
 * Prints placement information to a mapping-output file.
*/
void printPlacementResults(int gNumber, std::string gName, DirectedGraph *G, std::ofstream &positionedOutputFile, std::ofstream &unpositionedOutputFile, std::map<int, NodeConfig> *gConfig, json &UgrammPragmaConfig)
{
  float G_VisualX;
  float G_VisualY;

  if (!boost::get(&DotVertex::G_VisualX, *G, gNumber).empty()){
    G_VisualX = std::stof(boost::get(&DotVertex::G_VisualX, *G, gNumber)) * VISUAL_SCALE;
  }

  if (!boost::get(&DotVertex::G_VisualY, *G, gNumber).empty()){
    G_VisualY = std::stof(boost::get(&DotVertex::G_VisualY, *G, gNumber)) * VISUAL_SCALE;
  }
  //UGRAMM->debug("(printPlacementResults) visual stuff ok {}", gName);
  // Use for deciding the color of the FunCell based on the opcode
  // int opcode_gNumber = (*gConfig)[gNumber].opcode;
  int opcode_gNumber = 0;
  for (const auto& [key, value] : UgrammPragmaConfig.items())
  {
    if (key == (*gConfig)[gNumber].Type)
      break;
    else
      opcode_gNumber++;
  }
  // fix index being too high
  if (opcode_gNumber >= colors.size()) {
    opcode_gNumber = 0;
  }
  std::string modified_name = gNames_deliemter_changes(gName); // Modified combined string

  //UGRAMM->debug("(printPlacementResults) gname {} yields color index {} while color vector size is {}", gName, opcode_gNumber, colors.size());

  if ((*gConfig)[gNumber].Cell == "FUNCCELL")
  {
    //UGRAMM->debug("(printPlacementResults) gconfig cell ok {}", gName);
    if (((*Users)[gNumber].size() >= 1))
    {
      //UGRAMM->debug("(printPlacementResults) what causes segfault for {} : graphical? {}", gName, colors[opcode_gNumber]);

      positionedOutputFile << modified_name + "_" + funCellMapping[gName] << " [shape=\"rectangle\" width=0.5 fontsize=12 fillcolor=\"" << colors[opcode_gNumber] << "\" pos=\"" << G_VisualX << "," << G_VisualY << "!\"]\n";
      unpositionedOutputFile << modified_name + "_" + funCellMapping[gName] << " [shape=\"rectangle\" width=0.5 fontsize=12 fillcolor=\"" << colors[opcode_gNumber] << "\"]\n";
      // If FuncCell is used, then we have to do mandatary connections of FunCell to the input and output pins that are used.
      mandatoryFunCellConnections(gNumber, modified_name, G, positionedOutputFile, unpositionedOutputFile);
    }
    else
    {
      positionedOutputFile << modified_name << " [shape=\"rectangle\" width=0.5 fontsize=12 fillcolor=\"" << unused_cell_color << "\" pos=\"" << G_VisualX << "," << G_VisualY << "!\"]\n";
    }
  }
  else if ((*gConfig)[gNumber].Cell == "PINCELL")
  {

    //debug
    // UGRAMM->debug("pin {} has {} users", gName, (*Users)[gNumber].size());

    if (((*Users)[gNumber].size() >= 1))
    {
      //UGRAMM->debug("writing pin {}", gNames[gNumber]);
      if ((*gConfig)[gNumber].Type == "IN")
      {
        positionedOutputFile << modified_name << " [shape=\"oval\" width=0.1 fontsize=10 fillcolor=\"" << input_pin_color << "\" pos=\"" << G_VisualX << "," << G_VisualY << "!\"]\n";
        unpositionedOutputFile << modified_name << " [shape=\"oval\" width=0.1 fontsize=10 fillcolor=\"" << input_pin_color << "\"]\n";
      }
      else
      {
        positionedOutputFile << modified_name << " [shape=\"oval\" width=0.1 fontsize=10 fillcolor=\"" << output_pin_color << "\" pos=\"" << G_VisualX << "," << G_VisualY << "!\"]\n";
        unpositionedOutputFile << modified_name << " [shape=\"oval\" width=0.1 fontsize=10 fillcolor=\"" << output_pin_color << "\"]\n";
      }
    }
    else
    {
      positionedOutputFile << modified_name << " [shape=\"oval\" width=0.1 fontsize=10 fillcolor=\"" << unused_cell_color << "\" pos=\"" << G_VisualX << "," << G_VisualY << "!\"]\n";
    }
  }
}

/**
 * Prints mapping results in neato format: First displays the layout and then shows connections between the nodes.
 */
void printMappedResults(DirectedGraph *H, DirectedGraph *G, std::map<int, NodeConfig> *hConfig, std::map<int, NodeConfig> *gConfig, json &UgrammPragmaConfig)
{

  // Output stream for storing successful mapping: The positioned-output dot file stream (this contains actual co-ordinates of the node cells).
  std::ofstream positionedOutputFile;
  positionedOutputFile.open("positioned_dot_output.dot");
  UGRAMM->info("Writing the positioned mapping output in file 'positioned_dot_output.dot'");

  // Output stream for storing successful mapping:
  std::ofstream unpositionedOutputFile;
  unpositionedOutputFile.open("unpositioned_dot_output.dot");
  UGRAMM->info("Writing the unpositioned mapping output in file 'unpositioned_dot_output.dot'");

  // Printing the start of the dot file:
  positionedOutputFile << "digraph {\ngraph [bgcolor=lightgray];\n node [style=filled, fontname=\"times-bold\", penwidth=2];\n edge [penwidth=4]; \n splines=ortho;\n";
  //UGRAMM->debug("(printMappedResults) pos digraph OK");
  unpositionedOutputFile << "digraph {\ngraph [bgcolor=lightgray];\n node [style=filled, fontname=\"times-bold\", penwidth=2];\n edge [penwidth=4]; \n splines=true; rankdir=TB;\n";
  //UGRAMM->debug("(printMappedResults) UNpos digraph OK");
  //---------------------------------------------------
  // Adding nodes of kernel into simplified_file_output:
  //---------------------------------------------------
  unpositionedOutputFile << "subgraph cluster_1 {\n label = \"Input Kernel\"; fontsize = 40; style=dashed; \n edge [minlen=3]\n";
  for (int i = 0; i < num_vertices(*H); i++)
  {
    if(hNames[i] == "NULL")
      continue;
    unpositionedOutputFile << hNames[i] << ";\n";
  }
  //UGRAMM->debug("(printMappedResults) UNpos cluster1 vertices OK");

  std::pair<edge_iterator, edge_iterator> edge_pair = edges(*H);
  for (edge_iterator e_it = edge_pair.first; e_it != edge_pair.second; ++e_it)
  {
    vertex_descriptor u = source(*e_it, *H);
    vertex_descriptor v = target(*e_it, *H);

    if(hNames[u] == "NULL")
      continue;

    unpositionedOutputFile << "  " << hNames[u] << " -> " << hNames[v] << ";\n";
  }

  unpositionedOutputFile << "}\n";

  unpositionedOutputFile << "subgraph cluster_0 {\n label = \"UGRAMM mapping output\"; fontsize = 40; style=dashed;\n";

  //UGRAMM->debug("(printMappedResults) UNpos cluster1 EDGES OK");

  //------------------------
  // Draw/Print placement:
  //------------------------
  for (auto hElement : gNames)
  {
    int gNumber = hElement.first;
    //UGRAMM->debug("printing placement of cell {}", hElement.second);
    if ((FunCell_Visual_Enable & ((*gConfig)[gNumber].Cell == "FUNCCELL")) || (PinCell_Visual_Enable & ((*gConfig)[gNumber].Cell == "PINCELL")) || (RouteCell_Visual_Enable & ((*gConfig)[gNumber].Cell == "ROUTECELL")))
    {
      std::string gName = hElement.second;
      //UGRAMM->debug("ACTUALLY printing placement of cell {}", hElement.second);
      printPlacementResults(gNumber, gName, G, positionedOutputFile, unpositionedOutputFile, gConfig, UgrammPragmaConfig);
    }
  }

  //------------------------
  // Draw/Print routing:
  //------------------------
  for (int i = 0; i < num_vertices(*H); i++)
  {
    if(hNames[i] == "NULL")
      continue;
    printRoutingResults(i, positionedOutputFile, unpositionedOutputFile, hConfig, gConfig);
  }

  positionedOutputFile << "}\n";   // End of digraph
  unpositionedOutputFile << "}\n"; // End of second cluster
  unpositionedOutputFile << "}\n"; // End of digraph

}

/**
 * Prints the device model cell name corresponding to a given boost id number from device model graph.
 */
void printName(int n)
{
  UGRAMM->debug("{}", gNames[n]);
}

/**
 * Prints vertex models of the application graph's nodes.
 */
void printVertexModels(DirectedGraph *H, DirectedGraph *G, std::map<int, NodeConfig> *hConfig, std::map<int, int> &invUsers)
{
  for (int i = 0; i < num_vertices(*H); i++)
  {

    if(hNames[i] == "NULL")
      continue;

    struct RoutingTree *RT = &((*Trees)[i]);

    UGRAMM->info(" {} | ({})'s output pin :: ",  hNames[i], gNames[invUsers[i]]);
    std::list<int>::iterator it = RT->nodes.begin();

    //Checking for the elements with zero vertex model size (for the nodes without any fanout)
    if(RT->nodes.size() == 0)
    {
      UGRAMM->info("\t Empty vertex model (no-fanouts for the node)");
      //UGRAMM->debug("(printVertexModels) cell {} and maps to it {}", gNames[invUsers[i]], hNames[i]);
      funCellMapping[gNames[invUsers[i]]] = hNames[i];
    }
   
    while (it != RT->nodes.end())
    {
      int n = *it;
      UGRAMM->info("\t {} \t {}", n, gNames[n]);
      if (it == RT->nodes.begin())
      {
        // The begining node in the resource tree will be always outPin for the FunCell
        //   - Finding the FuncCell based on this outPin ID.
        //   - finding ID for alu_x_y from this: "alu_x_y.outPinA"
        int FunCellLoc = findFuncCellFromOutputPin(*it, G);

        // For concatinating the mapped applicationNodeID name in device model cell
        funCellMapping[gNames[FunCellLoc]] = hNames[i];
        //UGRAMM->debug("(printVertexModels) outputpin {} finds cell {} and maps to it {}", gNames[*it], gNames[FunCellLoc], hNames[i]);
      }

      it++;
    }
  }
}

/**
 * Prints the routing details for the given signal.
*/ 
void printRouting(int signal)
{

  struct RoutingTree *RT = &((*Trees)[signal]);
  UGRAMM->debug("** routing for i: {} {} ", signal, hNames[signal]);

  std::list<int>::iterator it = RT->nodes.begin();
  for (; it != RT->nodes.end(); it++)
  {
    UGRAMM->debug("\t {} \t {}", *it, gNames[*it]);
  }
}

//------------------------------------------------------------------------------------//
//-------------------------------- [Utilities] File read -----------------------------//
//------------------------------------------------------------------------------------//

/**
 * Reads a DOT file and stores attributes into the device model graph.
 */
void readDeviceModel(DirectedGraph *G, std::map<int, NodeConfig> *gConfig)
{
  //--------------------------------------------------------------------
  // Omkar:  Reading device model dot file instead.
  //--------------------------------------------------------------------

  // Iterate over all the nodes of the device model graph and assign the type of nodes:
  for (int i = 0; i < num_vertices(*G); i++)
  {
    vertex_descriptor v = vertex(i, *G);

    // Contains the Node type of Device Model Graph (FuncCell, RouteCell, PinCell)
    std::string archCellType = boost::get(&DotVertex::G_CellType, *G, v);
    std::string upperCaseCellType = boost::to_upper_copy(archCellType);
    (*gConfig)[i].Cell = upperCaseCellType;

    // Contains the Opcode of the NodeType (For example "ALU" for NodeType "FuncCell")
    std::string arch_NodeType = boost::get(&DotVertex::G_NodeType, *G, v);
    std::string upperCaseType = boost::to_upper_copy(arch_NodeType);
    (*gConfig)[i].Type = upperCaseType;

    // Contains the node name
    std::string arch_NodeName = boost::to_upper_copy(boost::get(&DotVertex::G_Name, *G, v));
    gNames[i] = arch_NodeName;
    gNamesInv[arch_NodeName] = i;

    if ((*gConfig)[i].Cell == "FUNCCELL"){
      gNamesInv_FuncCell[arch_NodeName] = i;
    }

    // Obtaining the loadPin name for PinCell type:
    if ((*gConfig)[i].Cell == "PINCELL")
    {
      size_t pos = gNames[i].find_last_of('.');
      if (pos != std::string::npos)
      {
        // UGRAMM->info("loadPin for {} is {}", gNames[i], gNames[i].substr(pos + 1));
        (*gConfig)[i].pinName = gNames[i].substr(pos + 1);
      }
    }

    // Fetching the width information:
    std::string G_width_readValue = boost::get(&DotVertex::G_Width, *G, v);
    if(G_width_readValue.empty()){
      (*gConfig)[i].width = 0;
    }
    else{
      (*gConfig)[i].width = std::stoi(G_width_readValue);
    }
    //OB: Debug
    UGRAMM->trace("[G] Width of {} = {}", gNames[i], (*gConfig)[i].width); 

    if (UGRAMM->level() <= spdlog::level::trace){
      UGRAMM->trace("[G] arch_NodeName {} :: arch_NodeCell {} :: arch_NodeType {}", arch_NodeName, upperCaseCellType, upperCaseType);
      if ((*gConfig)[i].Cell == "FUNCCELL"){
        UGRAMM->trace("[G] gNames[{}] {} :: gNamesInv[{}] {} :: gNamesInv_FuncCell[{}] {}", i, gNames[i], arch_NodeName, gNamesInv[arch_NodeName], arch_NodeName, gNamesInv_FuncCell[arch_NodeName]);
        UGRAMM->trace("[G] gName size {} :: gNameInv size {} :: gNameInv_FuncCell size {}", gNames.size(), gNamesInv.size(), gNamesInv_FuncCell.size());
      } else {
        UGRAMM->trace("[G] gNames[{}] {} :: gNamesInv[{}] {}", i, gNames[i], arch_NodeName, gNamesInv[arch_NodeName]);
        UGRAMM->trace("[G] gName size {} :: gNameInv {} :: gNameInv_FuncCell size() {}", gNames.size(), gNamesInv.size(), gNamesInv_FuncCell.size());
      }
    }
  }
}

/**
 * Reads a DOT file and stores attributes into the application graph.
*/ 
void readApplicationGraph(DirectedGraph *H, std::map<int, NodeConfig> *hConfig, std::map<int, NodeConfig> *gConfig)
{
  /*
    Application DFG is inputed in UGRAMM as a directed graph using the Neato Dot file convenction. In neeto,
    we have defined the vertices (also refered to as nodes) and edges with attributes (also refered to as properies).
    For instance, a node in UGRAMM will have a attribite to define the opcode or the edges may have the attribute to
    define the selective pins for an PE.
  */

  for (int i = 0; i < num_vertices(*H); i++)
  {

    vertex_descriptor v = vertex(i, *H);
    // Fetching node name from the application-graph:
    std::string name = boost::get(&DotVertex::H_Name, *H, v);
    hNames[i] = boost::to_upper_copy(removeCurlyBrackets(name)); // Removing if there are any curly brackets from the hNames.

    // Following characters are not supported in the neato visualizer: "|, =, ."
    std::replace(hNames[i].begin(), hNames[i].end(), '|', '_');
    std::replace(hNames[i].begin(), hNames[i].end(), '=', '_');
    std::replace(hNames[i].begin(), hNames[i].end(), '.', '_');

    hNamesInv[hNames[i]] = i;
    UGRAMM->trace("hNames[{}] {} :: hNamesInv[{}] {}", i, hNames[i], hNames[i], hNamesInv[hNames[i]]);

    // Fetching opcode from the application-graph:
    // Contains the Opcode of the operation (ex: FMUL, FADD, INPUT, OUTPUT etc.)
    std::string applicationOpcode = boost::get(&DotVertex::H_Opcode, *H, v);
    std::string upperCaseOpcode = boost::to_upper_copy(applicationOpcode);
    (*hConfig)[i].Opcode = upperCaseOpcode;

    // Fetching the width information:
    std::string H_width_readValue = boost::get(&DotVertex::H_Width, *H, v);
    if(H_width_readValue.empty()){
      (*hConfig)[i].width = 0;
    }
    else{
      (*hConfig)[i].width = std::stoi(H_width_readValue);
    }
    //OB: Debug
    UGRAMM->trace("[H] Width of {} = {}", hNames[i], (*hConfig)[i].width); 

    UGRAMM->trace("Condition :: {} :: Type :: {} ", skipPlacement((*hConfig)[i].Opcode, jsonParsed), (*hConfig)[i].Opcode);
 
    if (skipPlacement((*hConfig)[i].Opcode, jsonParsed))
    { 
      UGRAMM->info("[H] Ignoring placement for application node :: {} ", name);
      boost::clear_out_edges(v, *H);   //Removing all out-edges to and from vertex "v"
      boost::clear_in_edges(v,*H);     //Removing all in-edges to and from vertex "v"
      //boost::remove_vertex(v,*H);    //OB: Not removing vertex since it will impact the Adjacency list.
      (*hConfig)[i].Opcode = "NULL";   //Keeping the opcode Null for the removed vertex
      hNames[i] = "NULL";              //Keeping the opcode Null for the removed vertex
    }

    //Locking the device model nodes to a perticular node in the device model graph
    std::string jsonLockNode;
    if (needLocking(i, jsonParsed, jsonLockNode)){
      std::string delimiter = "::";
      size_t pos = jsonLockNode.find(delimiter);
      if (pos != std::string::npos) {
        std::string ParsedGName   = boost::to_upper_copy(jsonLockNode.substr(pos + delimiter.length()));
        std::string delimiter = "*";
        (*hConfig)[i].LockGNode = ParsedGName;
        if (!boost::algorithm::contains(ParsedGName, delimiter)){
          int gInverseID  = gNamesInv_FuncCell[ParsedGName];
          (*gConfig)[gInverseID].gLocked = true;
          UGRAMM->info("[Success] Application DFG node {} has one-to-one lock to the device model node {}", hNames[i], ParsedGName);
          UGRAMM->trace("[H] name {} :: applicationOpcode {} :: GNode Lock {} :: GNode Lock ID {}", hNames[i], upperCaseOpcode, ParsedGName, gInverseID);
        } else {
          std::string subStrings = ParsedGName;
          boost::trim_left_if(subStrings, boost::is_any_of("*"));
          boost::replace_all(subStrings, "*", ", "); 
          subStrings.insert(subStrings.begin(), '[');
          subStrings += "]";
          UGRAMM->info("[Success] Application DFG node {} can be locked to multiple device model node that contains the following substring {}", hNames[i], subStrings);
          UGRAMM->trace("[H] name {} :: applicationOpcode {} :: GNode Lock {}", hNames[i], upperCaseOpcode, ParsedGName);
        }
      } else {
        UGRAMM->error("[ERROR] Application DFG node {} defined a lock {} that using an invalid format as it does not have \"::\" ", hNames[i], jsonLockNode);
      }
    }

    UGRAMM->trace("[H] name {} :: applicationOpcode {} :: GNode Lock {}", hNames[i], upperCaseOpcode, (*hConfig)[i].LockGNode);
  }

  
  //Iterate over all edges of application graph:
  std::pair<edge_iterator, edge_iterator> edgeRange = boost::edges(*H);
  for (edge_iterator ei = edgeRange.first; ei != edgeRange.second; ++ei) {
    auto current_edge = *ei;

    vertex_descriptor currentEdgeSource = boost::source(current_edge, *H);
    vertex_descriptor currentEdgeTarget = boost::target(current_edge, *H);

    out_edge_iterator eo, eo_end;
    boost::tie(eo, eo_end) = out_edges(currentEdgeSource, *H);
    std::string H_LoadPin = boost::to_upper_copy(boost::get(&EdgeProperty::H_LoadPin, *H, *eo));
    std::string H_DriverPin = boost::to_upper_copy(boost::get(&EdgeProperty::H_DriverPin, *H, *eo));    

    UGRAMM->trace("[H] [BEFORE alias insertion ]edge from {} | {} -> {} | {}", hNames[currentEdgeSource], H_DriverPin, hNames[currentEdgeTarget], H_LoadPin);

    for(const auto& [key, value] : UgrammPragmaConfig.items()){
      if(key == H_LoadPin)
      {
        boost::put(&EdgeProperty::H_LoadPin, *H, *eo, value);
        UGRAMM->info("[H] [ALIAS insertion] edge from {} -> {} with loadPin {} to {}", hNames[currentEdgeSource], hNames[currentEdgeTarget], H_LoadPin, boost::get(&EdgeProperty::H_LoadPin, *H, *eo));
      }
    }
  }
  
}
