/* Copyright 2019 The Spin-Scenario Authors. All Rights Reserved.
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at
    http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/
#include "ssl_config.h"
#include <time.h>
#include <kernel/spinsys/isotope.h>

namespace ssl {
namespace utility {

sol::state *g_lua = nullptr;
CYacas *g_yacas = nullptr;
int omp_core_num = omp_get_num_procs();
vector<string> g_h5_string;

vec g_expmv_theta;

int g_openmp_core;

#ifdef WIN32
std::string g_project_path = "../../spin-scenario";
#else
string g_spin_scenario = "";
//std::string g_project_path = ".."; //"../spin-scenario DEBUG";
std::string g_project_path = "/usr/bin/.."; //"../spin-scenario"; UBUNTU DEB.
#endif

void set_openmp_core(int n) {
  g_openmp_core = n;
}
double g_inf = std::numeric_limits<double>::infinity();

vec load_expmv_theta() {
  string path = utility::g_project_path + "/share/spin-scenario/config/expmv_theta.txt";
  mat m = eigen_read(path);
  return m.col(0);
}

double g_pw90 = 5; // us.
void set_pw90(double val) {
  g_pw90 = val;
}

void set_pw90_api(const sol::table &t) {
  for (auto &kv : t) {
    double val = kv.second.as<double>();
    set_pw90(val);
    break;
  }
}

void set_max_grad_api(const sol::table &t) {
  for (auto &kv : t) {
    double val = kv.second.as<double>();
    set_max_grad(val);
    break;
  }
}

void set_max_slew_rate_api(const sol::table &t) {
  for (auto &kv : t) {
    double val = kv.second.as<double>();
    set_max_slew_rate(val);
    break;
  }
}
double g_max_grad = 50; // mT/m.
double g_max_slew_rate = 120; // T/m/s.

void set_max_grad(double val) {
  g_max_grad = val;
  g_seq_param->max_grad = val;
}

void set_max_slew_rate(double val) {
  g_max_slew_rate = val;
  g_seq_param->max_slew = val;
}

void set_grad(double max_amp, double max_slew_rate) {
  set_max_grad(max_amp);
  set_max_slew_rate(max_slew_rate);
}

double g_B0_ = -1; // tesla.
void set_B0_api(const sol::table &t) {
  for (auto &kv : t) {
    string s = kv.second.as<string>();
    set_B0(s);
    break;
  }
}

void set_B0(string mag) {
  boost::to_lower(mag);
  vector<string> str_vec;
  boost::split(str_vec, mag, boost::is_any_of(", "), boost::token_compress_on);
  double val_B0 = boost::lexical_cast<double>(str_vec[0]);
  if (str_vec[1] == "tesla" || str_vec[1] == "t") {
    g_B0_ = val_B0;
#ifdef SSL_OUTPUT_ENABLE
    string s = str(boost::format("%s %.3f Tesla.\n") % "magnet field set to be" % g_B0_);
    ssl_color_text("info", s);
#endif
    return;
  }
  //set_magnet_field(val_B0);
  if (str_vec[1] == "mhz") {
    ssl::spinsys::isotope proton("1H");
    double tesla = val_B0 * 2 * _pi / proton.gamma() * 1e6;
#ifdef SSL_OUTPUT_ENABLE
    string s = str(boost::format("%s %.3f MHz.\n") % "proton resonance frequency set to be" % val_B0);
    ssl_color_text("info", s);
#endif
    g_B0_ = tesla;
    return;
  }
  throw std::runtime_error("par error! static magnetic field not set yet！");
  //set_proton_freq(val_B0);
}

phantom_space g_phantom_space = phantom_space();

void reduce_phantom(const sol::table &t) {
  for (auto &kv : t) {
    sol::object key = kv.first;
    sol::object val = kv.second;

    string axis = key.as<string>();
    int index = val.as<int>();

    if (axis == "x0")
      g_phantom_space.x0 = index;

    if (axis == "x1")
      g_phantom_space.x1 = index;

    if (axis == "y0")
      g_phantom_space.y0 = index;

    if (axis == "y1")
      g_phantom_space.y1 = index;

    if (axis == "z0")
      g_phantom_space.z0 = index;

    if (axis == "z1")
      g_phantom_space.z1 = index;

    if (axis == "dx")
      g_phantom_space.dx = index;

    if (axis == "dy")
      g_phantom_space.dy = index;

    if (axis == "dz")
      g_phantom_space.dz = index;

  }
}

seq_param *g_seq_param = new seq_param;

herr_t op_func(hid_t loc_id, const char *name, const H5L_info_t *info, void *operator_data) {
  herr_t status;
  H5O_info_t infobuf;
  /*
   * Get type of the object and display its name and type.
   * The name of the object is passed to this function by
   * the Library.
   */
  status = H5Oget_info_by_name(loc_id, name, &infobuf, H5P_DEFAULT);
  switch (infobuf.type) {
    case H5O_TYPE_GROUP:g_h5_string.push_back(string(name));
      //printf("  Group: %s\n", name);
      break;
    case H5O_TYPE_DATASET:
      //printf("  Dataset: %s\n", name);
      break;
    case H5O_TYPE_NAMED_DATATYPE:
      //printf("  Datatype: %s\n", name);
      break;
    default:printf("  Unknown: %s\n", name);
  }
  return status;
}

cube h5read_cube(const H5File &file, string dataset_name) {
  DataSet dataset = file.openDataSet(dataset_name);
  DataSpace dataspace = dataset.getSpace();
  int Ndims = dataspace.getSimpleExtentNdims();
  if (Ndims != 3) {
    string s = "dims should be 3 for cube matrix, " + to_string(Ndims) + " in this dataset: " + dataset_name;
    throw std::runtime_error(s.c_str());
  }

  hsize_t dims[3];
  dataspace.getSimpleExtentDims(dims);
  // dims[0] z
  cube m((int) dims[2], (int) dims[1], (int) dims[0]); // ColMajor
  dataset.read(m.data(), PredType::NATIVE_DOUBLE);
  return m;
}
icube h5read_icube(const H5File &file, string dataset_name) {
  DataSet dataset = file.openDataSet(dataset_name);
  DataSpace dataspace = dataset.getSpace();
  int Ndims = dataspace.getSimpleExtentNdims();
  if (Ndims != 3) {
    string s = "dims should be 3 for cube matrix, " + to_string(Ndims) + " in this dataset: " + dataset_name;
    throw std::runtime_error(s.c_str());
  }

  hsize_t dims[3];
  dataspace.getSimpleExtentDims(dims);
  // dims[0] z
  icube m((int) dims[2], (int) dims[1], (int) dims[0]); // ColMajor
  dataset.read(m.data(), PredType::NATIVE_INT);
  return m;
}
mat h5read_mat(const H5File &file, string dataset_name) {
  DataSet dataset = file.openDataSet(dataset_name);
  DataSpace dataspace = dataset.getSpace();
  int Ndims = dataspace.getSimpleExtentNdims();
  if (Ndims != 2) {
    string s = "dims should be 2 for 2d matrix, " + to_string(Ndims) + " in this dataset: " + dataset_name;
    throw std::runtime_error(s.c_str());
  }

  hsize_t dims[2];
  dataspace.getSimpleExtentDims(dims);

  mat m((int) dims[1], (int) dims[0]); // ColMajor
  dataset.read(m.data(), PredType::NATIVE_DOUBLE);
  return m;
}
imat h5read_imat(const H5File &file, string dataset_name) {
  DataSet dataset = file.openDataSet(dataset_name);
  DataSpace dataspace = dataset.getSpace();
  int Ndims = dataspace.getSimpleExtentNdims();
  if (Ndims != 2) {
    string s = "dims should be 2 for 2d matrix, " + to_string(Ndims) + " in this dataset: " + dataset_name;
    throw std::runtime_error(s.c_str());
  }

  hsize_t dims[2];
  dataspace.getSimpleExtentDims(dims);

  imat m((int) dims[1], (int) dims[0]); // ColMajor
  dataset.read(m.data(), PredType::NATIVE_INT);
  return m;
}

void h5write(H5File &file, Group *group, string dataset_name, const string s) {
  try {
    StrType datatype(0, H5T_VARIABLE);
    DataSpace dataspace(H5S_SCALAR);
    DataSet dataset;
    if (group != nullptr)
      dataset = DataSet(group->createDataSet(dataset_name, datatype, dataspace));
    else
      dataset = DataSet(file.createDataSet(dataset_name, datatype, dataspace));
    dataset.write(s, datatype);
    dataspace.close();
    dataset.close();

    string group_name;
    if (group != nullptr)
      group_name = group->getObjnameByIdx(0);
    string s = "write string to h5 file [" + file.getFileName()
        + "] dataset [" + group_name + "/" + dataset_name + "]\n";

    ssl_color_text("info", s);
  }
    // catch failure caused by the DataSet operations
  catch (DataSetIException error) {
    error.printError();
  }

    // catch failure caused by the DataSpace operations
  catch (DataSpaceIException error) {
    error.printError();
  }
}

void h5write(H5File &file, Group *group, string dataset_name, const mat &m) {
  try {
    hsize_t dims[2];
    dims[0] = m.cols(); // ColMajor
    dims[1] = m.rows();
    DataSpace dataspace = DataSpace(2, dims);
    DataSet dataset;
    if (group != nullptr)
      dataset = DataSet(group->createDataSet(dataset_name, PredType::NATIVE_DOUBLE, dataspace));
    else
      dataset = DataSet(file.createDataSet(dataset_name, PredType::NATIVE_DOUBLE, dataspace));
    dataset.write(m.data(), PredType::NATIVE_DOUBLE);
    dataspace.close();
    dataset.close();
    string group_name;
    if (group != nullptr)
      group_name = group->getObjnameByIdx(0);
    string s = "write mat [" + to_string(m.rows()) + "*" + to_string(m.cols()) + "] to h5 file [" + file.getFileName()
        + "] dataset [" + group_name + "/" + dataset_name + "]\n";

    ssl_color_text("info", s);
  }
    // catch failure caused by the DataSet operations
  catch (DataSetIException error) {
    error.printError();
  }

    // catch failure caused by the DataSpace operations
  catch (DataSpaceIException error) {
    error.printError();
  }
}

void h5write(H5File &file, Group *group, string dataset_name, const vec &v) {
  try {
    hsize_t dims[1];
    dims[0] = v.size();
    DataSpace dataspace = DataSpace(1, dims);
    DataSet dataset;
    if (group != nullptr)
      dataset = DataSet(group->createDataSet(dataset_name, PredType::NATIVE_DOUBLE, dataspace));
    else
      dataset = DataSet(file.createDataSet(dataset_name, PredType::NATIVE_DOUBLE, dataspace));
    dataset.write(v.data(), PredType::NATIVE_DOUBLE);
    dataspace.close();
    dataset.close();
    string group_name;
    if (group != nullptr)
      group_name = group->getObjnameByIdx(0);
    string s = "write vec [" + to_string(v.size()) + "*1] to h5 file [" + file.getFileName()
        + "] dataset [" + group_name + "/" + dataset_name + "]\n";

    ssl_color_text("info", s);
  }
    // catch failure caused by the DataSet operations
  catch (DataSetIException error) {
    error.printError();
  }

    // catch failure caused by the DataSpace operations
  catch (DataSpaceIException error) {
    error.printError();
  }
}

void h5write(H5File &file, Group *group, string dataset_name, const ivec &iv) {
  try {
    hsize_t dims[1];
    dims[0] = iv.size();
    DataSpace dataspace = DataSpace(1, dims);
    DataSet dataset;
    if (group != nullptr)
      dataset = DataSet(group->createDataSet(dataset_name, PredType::NATIVE_INT, dataspace));
    else
      dataset = DataSet(file.createDataSet(dataset_name, PredType::NATIVE_INT, dataspace));
    dataset.write(iv.data(), PredType::NATIVE_INT);
    dataspace.close();
    dataset.close();
    string group_name;
    if (group != nullptr)
      group_name = group->getObjnameByIdx(0);
    string s = "write ivec [" + to_string(iv.size()) + "*1] to h5 file [" + file.getFileName()
        + "] dataset [" + group_name + "/" + dataset_name + "]\n";

    ssl_color_text("info", s);
  }
    // catch failure caused by the DataSet operations
  catch (DataSetIException error) {
    error.printError();
  }

    // catch failure caused by the DataSpace operations
  catch (DataSpaceIException error) {
    error.printError();
  }
}

void h5write(H5File &file, Group *group, string dataset_name, const icube &cube) {
  try {
    hsize_t dims[3];
    dims[0] = cube.dimension(2); // z
    dims[1] = cube.dimension(1); // y
    dims[2] = cube.dimension(0); // x
    DataSpace dataspace = DataSpace(3, dims);
    DataSet dataset;
    if (group != nullptr)
      dataset = DataSet(group->createDataSet(dataset_name, PredType::NATIVE_INT, dataspace));
    else
      dataset = DataSet(file.createDataSet(dataset_name, PredType::NATIVE_INT, dataspace));
    dataset.write(cube.data(), PredType::NATIVE_INT);
    dataspace.close();
    dataset.close();
    string group_name;
    if (group != nullptr)
      group_name = group->getObjnameByIdx(0);
    string s =
        "write icube [" + to_string(dims[2]) + "*" + to_string(dims[1]) + "*" + to_string(dims[0]) + "] to h5 file ["
            + file.getFileName()
            + "] dataset [" + group_name + "/" + dataset_name + "]\n";

    ssl_color_text("info", s);
  }
    // catch failure caused by the DataSet operations
  catch (DataSetIException error) {
    error.printError();
  }

    // catch failure caused by the DataSpace operations
  catch (DataSpaceIException error) {
    error.printError();
  }
}

void declare_path(const char *ptr2) {
  std::ostringstream os;

  if (ptr2[strlen(ptr2) - 1] != PATH_SEPARATOR)
    os << "DefaultDirectory(\"" << ptr2 << PATH_SEPARATOR_2 << "\");";
  else
    os << "DefaultDirectory(\"" << ptr2 << "\");";

  g_yacas->Evaluate(os.str());
  if (g_yacas->IsError())
    std::cout << "Failed to set default directory: " << g_yacas->Error() << "\n";
}
void load_yacas() {
  string root_dir = g_project_path + "/share/spin-scenario/config/yacas/scripts";
  g_yacas = new CYacas(std::cout);
  /* Split up root_dir in pieces separated by colons, and run
  DefaultDirectory on each of them. */
  const char *ptr1, *ptr2;
  ptr1 = ptr2 = root_dir.c_str();
  while (*ptr1 != '\0') {
    while (*ptr1 != '\0' && *ptr1 != ';')
      ptr1++;
    if (*ptr1 == ';') {
      const std::string path(ptr2, ptr1);
      declare_path(path.c_str());
      ptr1++;
      ptr2 = ptr1;
    }
  }
  declare_path(ptr2);

  std::ostringstream os;
  os << "Load(\"yacasinit.ys\");";
  g_yacas->Evaluate(os.str());
  if (g_yacas->IsError())
    ssl_color_text("err", "loading Yacas library failed.");
}

void yacas_global_vars() {
  ssl::spinsys::isotope H1("1H");
  //cout<<H1.gamma()<<"\n";
  g_yacas->Evaluate("gamma1H :=" + to_string(H1.gamma() / 2 / _pi)); // unit in Hz/T.
}

string yacas_evaluate(const string expr) {
  g_yacas->Evaluate(expr);
  string s = g_yacas->Result();
  if (boost::ends_with(s, ";"))
    boost::erase_last(s, ";");
  return s;
}

state_par state_evaluate(const string expr) {
  state_par s;
  vector<int> spin_id;
  boost::smatch swhat;
  boost::regex reg_nspin("I(\\d+)");
  std::string::const_iterator start = expr.begin();
  std::string::const_iterator end = expr.end();
  while (boost::regex_search(start, end, swhat, reg_nspin)) {
    spin_id.push_back(boost::lexical_cast<int>(swhat[1]));
    //std::cout << swhat[1] << std::endl;
    start = swhat[0].second;
  }

  auto max_id = std::max_element(std::begin(spin_id), std::end(spin_id));

  vector<string> par1;
  vector<string> par2;
  for (int i = 0; i < *max_id; i++) {
    string id = boost::lexical_cast<string>(i + 1);
    string Iix = "I" + id + "x";
    string Iiy = "I" + id + "y";
    string Iiz = "I" + id + "z";

    par1.push_back(Iix);
    par1.push_back(Iiy);
    par1.push_back(Iiz);

    string Iip = "I" + id + "p";
    string Iim = "I" + id + "m";

    string Ix = Iix + ":= 0.5*(" + Iip + "+" + Iim + ")";
    string Iy = Iiy + ":= -0.5*I*(" + Iip + "-" + Iim + ")";
    par2.push_back(Ix);
    par2.push_back(Iy);
  }

  string expr_def = "[Local(";
  for (size_t i = 0; i < par1.size() - 1; i++)
    expr_def += par1[i] + ",";
  expr_def += par1.back() + ");";

  for (size_t i = 0; i < par2.size(); i++)
    expr_def += par2[i] + ";";

  string expr_in = expr, expr_out;
  boost::replace_all(expr_in, "xI", "x*I");
  boost::replace_all(expr_in, "yI", "y*I");
  boost::replace_all(expr_in, "zI", "z*I");
  boost::replace_all(expr_in, "eI", "e*I");
  expr_in = "Simplify(" + expr_in + ");]";
  expr_def += expr_in;
  expr_out = yacas_evaluate(expr_def);
  //cout << "expr:" << expr_def << endl;
  //cout << "expr in:" << expr_in << endl;
  //cout << "expr out:" << expr_out << endl;

  boost::cmatch what;
  boost::regex reg_plus("([^\\,|^\\(])(\\-)");
  if (boost::regex_search(expr_out.c_str(), what, reg_plus)) {
    //cout << string(what[1]) << "\n" << string(what[2]) << "\n";
    string s = boost::regex_replace(expr_out, reg_plus, "$1+$2");
    expr_out = s;
    //cout << expr_out << "\n";
  }

  if (boost::starts_with(expr_out, "-"))
    boost::replace_first(expr_out, "-", "+-");

  boost::regex reg_unity("(\\+)(\\-)(I)");
  if (boost::regex_search(expr_out.c_str(), what, reg_unity)) {
    //cout << string(what[1]) << "\n" << string(what[2]) << "\n";
    string s = boost::regex_replace(expr_out, reg_unity, "$1-1*$3");
    expr_out = s;
    //cout << expr_out << "\n";
  }

  boost::erase_all(expr_out, ";");
  if (boost::starts_with(expr_out, "+"))
    boost::erase_first(expr_out, "+");
  vector<string> expr_vec;
  boost::algorithm::split(expr_vec, expr_out, boost::is_any_of("+"));

  vector<cd> coeff(expr_vec.size());
  for (size_t i = 0; i < expr_vec.size(); i++) {

    if (expr_vec[i] == "1")
      expr_vec[i] = "I1e";

    //cout << expr_vec[i] << "\n";
    boost::regex reg_complex("(\\*Complex\\()(\\-?\\d+\\.?\\d*)(\\,)(\\-?\\d+\\.?\\d*)(\\))");
    if (boost::regex_search(expr_vec[i].c_str(), what, reg_complex)) {
      coeff[i] = cd(stod(what[2]), stod(what[4]));
      string s = boost::regex_replace(expr_vec[i], reg_complex, "");
      expr_vec[i] = s;
    } else {
      boost::regex reg_double("\\(?(\\-?\\d+\\.?\\d*)\\)?\\*");
      if (boost::regex_search(expr_vec[i].c_str(), what, reg_double)) {
        coeff[i] = cd(stod(what[1]), 0);
        //cout << coeff.back() << "\n";
        string s = boost::regex_replace(expr_vec[i], reg_double, "");
        expr_vec[i] = s;
      } else {
        coeff[i] = cd(1, 0);
      }
    }

    boost::erase_all(expr_vec[i], "*");
    boost::regex reg("I(\\d)(\\w)");
    string s = boost::regex_replace(expr_vec[i], reg, "$1 I$2 ");
    expr_vec[i] = s;
    //cout << coeff[i] << "\n";
    //cout << expr_vec[i] << "\n";
  }
  s.coeff = coeff;
  s.expr = expr_vec;
  return s;
}

double yacas_integral(double from, double to, string func, string var, int precision) {
  string expr = "N(Integrate(" + var + "," + std::to_string(from) + "," + std::to_string(to) + ") (" + func + "),"
      + std::to_string(precision) + ")";
  cout << expr << "\n";
  string s = yacas_evaluate(expr);
  cout << s << "\n";
  return std::stod(s);
}

string yacas_integral(string func, string var) {
  string expr = "Integrate(" + var + ") (" + func + ")";
  return yacas_evaluate(expr);
}

double yacas_func(double pos, string func, string var, int precision) {
  string expr = "N(f(" + var + "):=" + func + ")";
  return std::stod(yacas_evaluate(expr));
}

char *sys_time() {
  time_t lt;
  time(&lt);
  struct tm *stm;
  stm = localtime(&lt);
  char *buffer = new char[80];
  strftime(buffer, 80, "%Y%m%d_%H%M%S", stm);
  return buffer;
  // http://www.cnblogs.com/likwo/archive/2012/08/30/2663242.html
}
void ssl_version_output() {
  cout << endl;
  cout << boost::format("%10tS P I N  S C E N A R I O") << endl;
  cout << boost::format("%10t%s") % "version 1.0   last modified 2019-2-20" << endl;
  cout << endl;
  cout << boost::format("%10t%s") % "Copyright(C) 2019" << endl;
  cout << boost::format("%10t%s") % "Yan Chang (changy@sibet.ac.cn)" << endl;
  cout << endl;
  cout << boost::format("%10t%s") % "https://github.com/spin-scenario" << endl;
}

#ifdef WIN32
HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
WORD wOldColorAttrs = get_old_color_attrs();
WORD get_old_color_attrs() {
    CONSOLE_SCREEN_BUFFER_INFO csbiInfo;
    GetConsoleScreenBufferInfo(hConsole, &csbiInfo);
    return csbiInfo.wAttributes;
}
#endif

void ssl_color_text(const string &option, const string &s, ostream &ostr) {
#ifdef WIN32
  if (option == "info") {
      SetConsoleTextAttribute(hConsole, SSL_INFO_COLOR1);
      ostr << "SSL-Info:" << flush;
      SetConsoleTextAttribute(hConsole, SSL_INFO_COLOR2);
  }
  if (option == "warn") {
      SetConsoleTextAttribute(hConsole, SSL_WARN_COLOR1);
      ostr << "SSL-Warn:" << flush;
      SetConsoleTextAttribute(hConsole, SSL_WARN_COLOR2);
  }
  if (option == "err") {
      SetConsoleTextAttribute(hConsole, SSL_ERR_COLOR1);
      ostr << "SSL-Err:" << flush;
      SetConsoleTextAttribute(hConsole, SSL_ERR_COLOR2);
  }
  ostr << " " << s;
  SetConsoleTextAttribute(hConsole, wOldColorAttrs);
#else
  if (option == "info") {
    ostr << "\x1b[1;32mSSL-Info:" << flush;
  }
  if (option == "warn") {
    ostr << "\x1b[1;33mSSL-Warn:" << flush;
  }
  if (option == "err") {
    ostr << "\x1b[1;31mSSL-Err:" << flush;
  }
  ostr << " " << s << "\x1b[0m";
#endif
}
stft_out stft(const cx_vec &signal, win_shape wshape, int win_length, int hop, int nfft, double fs) {
  // length of the signal.
  int sig_length = signal.size();
  vec win_func = window_function(wshape, win_length);

  // form the stft matrix.
  //int rows = ceil((1 + nfft) / 2.0);
  int rows = nfft;
  int cols = 1 + int((sig_length - win_length) / (double) hop);
  cx_mat specgram = cx_mat::Zero(rows, cols); // form the stft matrix

  // perform STFT.
  int time_pos = 0;
  int col_idx = 0;
  while (time_pos + win_length < sig_length) {
    // windowing
    cx_vec local_sig = signal.segment(time_pos, win_length);
    cx_vec local_win_sig = local_sig.cwiseProduct(win_func);
    cx_vec local_spec = fft(local_win_sig, nfft);
    specgram.col(col_idx) = local_spec.segment(0, rows)/*.reverse()*/;
    // update the indexes
    time_pos += hop;
    col_idx++;
  }
  // calculate the time and frequency vectors.
  double delta_time = hop / fs;
  double delta_freq = fs / nfft;
  vec time = vec::LinSpaced(cols, win_length / 2, win_length / 2 + (cols - 1) * hop);
  time *= 1 / fs;
  vec freq = vec::LinSpaced(rows, -rows / 2, rows / 2);
  freq *= delta_freq;

  return stft_out(specgram, time, freq, delta_time, delta_freq);
}

extern const std::map<string, double> g_phase_map = phase_map();

std::map<string, double> phase_map() {
  std::map<string, double> phase;
  phase.insert(pair<string, double>("x", 0));
  phase.insert(pair<string, double>("y", 90));
  phase.insert(pair<string, double>("-x", 180));
  phase.insert(pair<string, double>("-y", 270));
  return phase;
}

cd fid_xy2amp(cd val) {
  return cd(abs(val), phase_in_rad(val));
}

cd fid_amp2xy(cd val) {
  double x = val.real() * cos(val.imag());
  double y = val.real() * sin(val.imag());
  return cd(x, y);
}

cd xy2amp(cd val) {
  return cd(abs(val), deg2rad(phase_in_degree(val)));
}
cd amp2xy(cd val) {
  double x = val.real() * cos(val.imag());
  double y = val.real() * sin(val.imag());
  return cd(x, y);
}
double phase_in_rad(cd val) {
  double phase = 0;
  double ux = val.real();
  double uy = val.imag();
  /* 0~360 degree */
  if (uy > 0) {
    if (ux > 0)
      phase = atan(uy / ux);
    else if (ux < 0)
      phase = _pi + atan(uy / ux);
    else
      phase = _pi / 2;
  } else if (uy < 0) {
    if (ux > 0)
      phase = 2 * _pi + atan(uy / ux);
    else if (ux < 0)
      phase = _pi + atan(uy / ux);
    else
      phase = _pi * 3 / 2;
  } else {
    if (ux >= 0)
      phase = 0;
    else if (ux < 0)
      phase = _pi;
  }
  return phase;
}
double phase_in_degree(cd val) {
  return phase_in_rad(val) * 180.0 / _pi;
}

std::map<string, win_shape> win_shape_map() {
  std::map<string, win_shape> map;
  map.insert(pair<string, win_shape>("gauss", _gauss));
  map.insert(pair<string, win_shape>("hamming", _hamming));
  map.insert(pair<string, win_shape>("rect", _rect));
  return map;
}

win_shape window_interpreter(string win_name) {
  boost::to_lower(win_name);
  std::map<string, win_shape>::const_iterator iter;
  for (size_t i = 0; i < g_win_shape.size(); i++) {
    iter = g_win_shape.find(win_name);
    if (iter != g_win_shape.end())
      return iter->second;
  }
  string s = "unknown window name: " + win_name;
  throw std::runtime_error(s.c_str());
  return _unknown_window;
}

vec window_function(win_shape wshape, int sig_length) {
  vec win = vec::Zero(sig_length);
  switch (wshape) {
    case _hamming: {
      for (int i = 0; i < sig_length; i++)
        win[i] = 0.54 - 0.46 * cos((2.0 * _pi * (i + 1.0)) / (sig_length + 1.0));
    }
      break;
    case _gauss: {
      double K = 0.005;
      for (int i = 0; i < sig_length; i++) {
        double indice = -1.0 + (2.0 * i) / (sig_length - 1.0);
        win[i] = exp((indice * indice) * log(K));
      }
    }
      break;
    case _rect: {
      win = vec::Ones(sig_length);
    }
      break;
    default:break;
  }
  return win;
  // http://forge.scilab.org/index.php/p/stftb/source/tree/master/sci_gateway/c/create_window.c
}

cx_vec fft(const cx_vec &src, int nfft) {
  int i;
  fftw_complex *in;
  fftw_complex *out;
  fftw_plan plan_forward;

  in = (fftw_complex *) fftw_malloc(sizeof(fftw_complex) * nfft);
  int n = src.size();
  for (i = 0; i < nfft; i++) {
    if (i < n) {
      in[i][0] = src[i].real();
      in[i][1] = src[i].imag();
    } else {
      in[i][0] = 0;
      in[i][1] = 0;
    }
  }

  out = (fftw_complex *) fftw_malloc(sizeof(fftw_complex) * nfft);
  plan_forward = fftw_plan_dft_1d(nfft, in, out, FFTW_FORWARD, FFTW_ESTIMATE);
  fftw_execute(plan_forward);

  cx_vec result(nfft);
  /*for (i = 0; i < nfft; i++)
      result[i] = cd(out[i][0], out[i][1]);*/
  for (i = 0; i < nfft / 2; i++)
    result[i] = cd(out[i + nfft / 2][0], out[i + nfft / 2][1]);
  for (i = nfft / 2; i < nfft; i++)
    result[i] = cd(out[i - nfft / 2][0], out[i - nfft / 2][1]);

  return result;
}
cx_vec fft_1d(const cx_vec &src) {
  int N = src.size();
  fftw_complex *in, *out;
  fftw_plan p;
  in = (fftw_complex *) fftw_malloc(sizeof(fftw_complex) * N);
  cd val;
  double flag;
  for (int i = 0; i < N; i++) {
    flag = pow(-1, i);
    val = src(i);
    in[i][0] = flag * val.real();
    in[i][1] = flag * val.imag();
  }
  out = (fftw_complex *) fftw_malloc(sizeof(fftw_complex) * N);
  p = fftw_plan_dft_1d(N, in, out, FFTW_FORWARD, FFTW_ESTIMATE);

  fftw_execute(p); /* repeat as needed */
  fftw_destroy_plan(p);
  fftw_free(in);

  cx_vec des = cx_vec(N);
  for (int i = 0; i < N; i++)
    des(i) = cd(out[i][0], out[i][1]);
  fftw_free(out);
  return des;
}
cx_vec fft_1d(const sol::table &t) {
  int N = t.size();
  cx_vec src(N);
  for (int i = 0; i < N; i++) {
    sol::object val = t[i + 1];
    src[i] = val.as<cd>();
  }
  return fft_1d(src);
}
cx_mat fft_2d(const cx_mat &src) {
  int n0 = src.rows();
  int n1 = src.cols();
  int N = n0 * n1;
  fftw_complex *in, *out;
  fftw_plan p;
  in = (fftw_complex *) fftw_malloc(sizeof(fftw_complex) * N);
  cd val;
  double flag;  // dimensions of your array are even
  for (int i = 0; i < n0; i++)
    for (int j = 0; j < n1; j++) {
      flag = pow(-1, i + j);
      val = src(i, j);
      in[j + i * n1][0] = flag * val.real();
      in[j + i * n1][1] = flag * val.imag();
    }
  out = (fftw_complex *) fftw_malloc(sizeof(fftw_complex) * N);
  p = fftw_plan_dft_2d(n0, n1, in, out, FFTW_FORWARD,
                       FFTW_ESTIMATE);
  fftw_execute(p);
  fftw_destroy_plan(p);
  fftw_free(in);

  cx_mat des = cx_mat::Zero(n0, n1);
  for (int i = 0; i < n0; i++)
    for (int j = 0; j < n1; j++)
      des(i, j) = cd(out[j + i * n1][0], out[j + i * n1][1]);
  fftw_free(out);
  return des;
}
mat eigen_read(string file_name) {
  ifstream file;
  file.open(file_name.c_str(), ios::in);
  if (!file) {
    string s = "failed to read file " + file_name;
    throw std::runtime_error(s.c_str());
  }
  vector<vector<string>> doc_str;
  string line;
  while (!file.eof()) {
    getline(file, line);
    if (!boost::starts_with(line, "#")) {
      boost::trim(line); // trim the spaces on both left and right sides.
      if (strlen(line.c_str()) == 0)
        continue;
      vector<string> line_vec_str;
      boost::split(line_vec_str, line, boost::is_any_of(" , "), boost::token_compress_on);
      doc_str.push_back(line_vec_str);
    }
  }
  file.clear();
  file.close();

  int nrows = doc_str.size();
  int ncols = doc_str[0].size(); // we suppose size of each line is identical.
  mat result(nrows, ncols);
  result.setZero();
  omp_set_num_threads(omp_core_num);
#pragma omp parallel for
  for (int i = 0; i < (int) doc_str.size(); i++) {
    for (int j = 0; j < ncols; j++)
      result(i, j) = boost::lexical_cast<double>(doc_str[i].at(j));
  }
  return result;
}
cd traced(const sp_cx_mat &m) {
  cd sum = 0;
  for (int k = 0; k < m.outerSize(); ++k)
    sum += m.coeff(k, k);
  return sum;
}
sp_cx_mat mul(const sp_mat &a, cd b) {
  return a * b;
}
sp_cx_mat mul(cd a, const sp_mat &b) {
  return a * b;
}
sp_cx_mat mul(const sp_cx_mat &a, cd b) {
  return a * b;
}
sp_cx_mat mul(cd a, const sp_cx_mat &b) {
  return a * b;
}
sp_cx_vec mul(const sp_cx_vec &a, cd b) {
  return a * b;
}
sp_cx_vec mul(cd a, const sp_cx_vec &b) {
  return a * b;
}
sp_cx_mat div(const sp_mat &a, cd b) {
  return a * (cd(1, 0) / b);
}
sp_cx_mat div(const sp_cx_mat &a, cd b) {
  return a * (cd(1, 0) / b);
}
sp_cx_vec div(const sp_cx_vec &a, cd b) {
  return a * (cd(1, 0) / b);
}

void write(string file, sol::variadic_args va, const mat & /*m*/) {
  string sep = "\n#----------------------------------------\n";
  ofstream ofstr(file.c_str());
  //ofstr << sys_time() << sep;
  ofstr.precision(3);
  for (auto v : va) {
    mat val = v;
    /*Eigen::IOFormat OctaveFmt(Eigen::StreamPrecision, 0, ", ", ";\n", "", "", "[", "]");
    ofstr << v.format(OctaveFmt) << sep;*/
    ofstr << val;// << sep;
  }
  ofstr.close();

}
void write(string file, sol::variadic_args va, const cx_mat & /*m*/) {
  string sep = "\n#----------------------------------------\n";
  ofstream ofstr(file.c_str());
  ofstr << sys_time() << sep;
  ofstr.precision(3);
  for (auto v : va) {
    cx_mat val = v;
    /*Eigen::IOFormat OctaveFmt(Eigen::StreamPrecision, 0, ", ", ";\n", "", "", "[", "]");
    ofstr << v.format(OctaveFmt) << sep;*/
    ofstr << val << sep;
  }
  ofstr.close();

}
void write(string file, sol::variadic_args va, const vec & /*v*/) {
  string sep = "\n#----------------------------------------\n";
  ofstream ofstr(file.c_str());
  ofstr << sys_time() << sep;
  ofstr.precision(3);
  for (auto v : va) {
    vec val = v;
    /*Eigen::IOFormat OctaveFmt(Eigen::StreamPrecision, 0, ", ", ";\n", "", "", "[", "]");
    ofstr << v.format(OctaveFmt) << sep;*/
    ofstr << val << sep;
  }
  ofstr.close();
}
void write(string file, sol::variadic_args va, const cx_vec & /*v*/) {
  string sep = "\n#----------------------------------------\n";
  ofstream ofstr(file.c_str());
  ofstr << sys_time() << sep;
  ofstr.precision(3);
  for (auto v : va) {
    cx_vec val = v;
    /*Eigen::IOFormat OctaveFmt(Eigen::StreamPrecision, 0, ", ", ";\n", "", "", "[", "]");
    ofstr << v.format(OctaveFmt) << sep;*/
    ofstr << val << sep;
  }
  ofstr.close();
}
void write(string file, sol::variadic_args va, const sp_mat & /*m*/) {
  string sep = "\n#----------------------------------------\n";
  ofstream ofstr(file.c_str());
  ofstr << sys_time() << sep;
  ofstr.precision(3);
  for (auto v : va) {
    sp_mat val = v;
    /*Eigen::IOFormat OctaveFmt(Eigen::StreamPrecision, 0, ", ", ";\n", "", "", "[", "]");
    ofstr << v.toDense().format(OctaveFmt) << sep;*/
    ofstr << val << sep;
  }
  ofstr.close();
}
void write(string file, sol::variadic_args va, const sp_cx_mat & /*m*/) {
  string sep = "\n#----------------------------------------\n";
  ofstream ofstr(file.c_str());
  ofstr << sys_time() << sep;
  ofstr.precision(3);
  for (auto v : va) {
    sp_cx_mat val = v;
    /*Eigen::IOFormat OctaveFmt(Eigen::StreamPrecision, 0, ", ", ";\n", "", "", "[", "]");
    ofstr << v.toDense().format(OctaveFmt) << sep;*/
    ofstr << val << sep;
  }
  ofstr.close();
}
void write(string file, sol::variadic_args va, const sp_vec & /*v*/) {
  string sep = "\n#----------------------------------------\n";
  ofstream ofstr(file.c_str());
  ofstr << sys_time() << sep;
  ofstr.precision(3);
  for (auto v : va) {
    sp_vec val = v;
    /*Eigen::IOFormat OctaveFmt(Eigen::StreamPrecision, 0, ", ", ";\n", "", "", "[", "]");
    ofstr << v.toDense().format(OctaveFmt) << sep;*/
    ofstr << val << sep;
  }
  ofstr.close();
}
void write(string file, sol::variadic_args va, const sp_cx_vec & /*v*/) {
  string sep = "\n#----------------------------------------\n";
  ofstream ofstr(file.c_str());
  ofstr << sys_time() << sep;
  ofstr.precision(3);
  for (auto v : va) {
    sp_cx_vec val = v;
    /*Eigen::IOFormat OctaveFmt(Eigen::StreamPrecision, 0, ", ", ";\n", "", "", "[", "]");
    ofstr << v.toDense().format(OctaveFmt) << sep;*/
    ofstr << val << sep;
  }
  ofstr.close();
}

bool is_retrievable(string key, const sol::table &t) {
  sol::object val = t[key];
  return val.valid();
}
sol::object retrieve_table(string key, const sol::table &t, string supp) {
  sol::object val = t[key];
  if (!val.valid()) {
    string s = "no '" + key + "' key inside the this table.";
    if (!supp.empty())
      s += " (" + supp + ")";
    throw std::runtime_error(s.c_str());
  }
  return val;
}

string retrieve_table_str(string key, const sol::table &t, string supp) {
  sol::object val = retrieve_table(key, t, supp);
  return val.as<string>();
}
int retrieve_table_int(string key, const sol::table &t, string supp) {
  sol::object val = retrieve_table(key, t, supp);
  return val.as<int>();
}
size_t retrieve_table_size_t(string key, const sol::table &t, string supp) {
  sol::object val = retrieve_table(key, t, supp);
  return val.as<size_t>();
}

double retrieve_table_double(string key, const sol::table &t, string supp) {
  sol::object val = retrieve_table(key, t, supp);
  return val.as<double>();
}

bool parse(string exp, colon_sep &val) {
  boost::cmatch what;
  boost::regex reg("\\s*(\\-?\\d+\\.?\\d*)\\s*:\\s*(\\-?\\d+\\.?\\d*)\\s*:\\s*(\\-?\\d+\\.?\\d*)\\s*");
  if (boost::regex_search(exp.c_str(), what, reg)) {
    val.a = boost::lexical_cast<double>(what[1]);
    val.b = boost::lexical_cast<double>(what[2]);
    val.num = boost::lexical_cast<int>(what[3]);
    return true;
  } else {
    return false;
    //string s = "parse a:b:c failed: " + exp;
    //throw std::runtime_error(s.c_str());
  }
}

vec stl2vec(const vector<double> &data) {
  vec v(data.size());
  for (size_t i = 0; i < data.size(); i++)
    v[i] = data[i];
  return v;
}

void apodization(bool app, double decay_rate) {

}

void apodization(cx_vec &fid, double decay_rate,
                 const WindowFunction wf) {
  switch (wf) {
    case kWF_exp_1d: {
      vec d01 = vec::LinSpaced(fid.size(), 0, 1);
      fid(0) /= 2;
      for (size_t i = 0; i < fid.size(); i++)
        fid(i) *= exp((double) (-decay_rate * d01(i)));
    }
      break;
    case kWF_crisp_1d: {
      vec d01 = vec::LinSpaced(fid.size(), 0, _pi / 2);
      fid(0) /= 2;
      for (size_t i = 0; i < fid.size(); i++)
        fid(i) *= pow(cos(d01(i)), 8);
    }
      break;
    default:break;
  }

}
}
}

