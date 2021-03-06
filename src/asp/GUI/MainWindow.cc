// __BEGIN_LICENSE__
//  Copyright (c) 2006-2013, United States Government as represented by the
//  Administrator of the National Aeronautics and Space Administration. All
//  rights reserved.
//
//  The NGT platform is licensed under the Apache License, Version 2.0 (the
//  "License"); you may not use this file except in compliance with the
//  License. You may obtain a copy of the License at
//  http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.
// __END_LICENSE__


/// \file MainWindow.cc
///
/// The stereo_gui main window class.
///
#include <QtGui>
#include <asp/GUI/MainWindow.h>
#include <asp/GUI/MainWidget.h>
using namespace vw::gui;

#include <vw/config.h>
#include <vw/Image/MaskViews.h>
#include <vw/FileIO/DiskImageView.h>
#include <vw/FileIO/DiskImageResource.h>
#include <vw/Image/Statistics.h>
#include <vw/Image/PixelMask.h>
#include <vw/InterestPoint/InterestData.h>
#include <vw/InterestPoint/Matcher.h>
#include <boost/filesystem/path.hpp>

#include <sstream>
namespace po = boost::program_options;

MainWindow::MainWindow(asp::BaseOptions const& opt,
                       std::vector<std::string> const& images,
                       std::string const& output_prefix,
                       int grid_cols,
                       vw::Vector2i const& window_size,
                       bool single_window,
                       bool use_georef, bool hillshade,
                       int argc,  char ** argv) :
  m_opt(opt),
  m_output_prefix(output_prefix), m_widRatio(0.3), m_chooseFiles(NULL),
  m_grid_cols(grid_cols),
  m_use_georef(use_georef), m_hillshade(hillshade),
  m_argc(argc), m_argv(argv) {

  resize(window_size[0], window_size[1]);

  // Set the window title and add tabs
  std::string window_title = "Stereo GUI";
  this->setWindowTitle(window_title.c_str());

  // Set up the basic layout of the window and its menus.
  createMenus();

  // Collect only the valid images
  m_images.clear();
  for (size_t i = 0; i < images.size(); i++) {
    bool is_image = true;
    try {
      DiskImageView<double> img(images[i]);
    }catch(...){
      is_image = false;
    }
    if (!is_image) continue;
    m_images.push_back(images[i]);
  }

  if (m_images.empty()) {
    popUp("No valid images to display.");
    return;
  }

  m_matches_were_loaded = false;
  m_matches.clear();
  m_matches.resize(m_images.size());

  m_view_type = VIEW_SIDE_BY_SIDE;
  if (m_grid_cols > 1) {
    m_view_type = VIEW_AS_TILES_ON_GRID;
  }
  if (single_window) {
    m_view_type = VIEW_IN_SINGLE_WINDOW;
  }
  m_view_type_old = m_view_type;

  createLayout();
}


void MainWindow::createLayout() {

  // Create a new central widget. Qt is smart enough to de-allocate
  // the previous widget and all of its children.

  QWidget * centralWidget = new QWidget(this);
  setCentralWidget(centralWidget);

  QSplitter * splitter = new QSplitter(centralWidget);

  // By default, show the images in one row
  if (m_grid_cols <= 0)
    m_grid_cols = std::numeric_limits<int>::max();

  // Wipe the widgets from the array. Qt will automatically delete
  // the widgets when the time is right.
  m_widgets.clear();

  if (m_view_type == VIEW_IN_SINGLE_WINDOW) {
    // Put all images in a single window, with a dialog for choosing images if
    // there's more than one image.

    if (m_images.size() > 1) {
      m_chooseFiles = new chooseFilesDlg(this);
      m_chooseFiles->setMaximumSize(int(m_widRatio*size().width()), size().height());
      splitter->addWidget(m_chooseFiles);
    }

    MainWidget * widget = new MainWidget(centralWidget,
                                         m_opt,
                                         0, m_output_prefix,
                                         m_images, m_matches,
                                         m_chooseFiles,
                                         m_use_georef, m_hillshade);
    m_widgets.push_back(widget);

  } else{

    for (size_t i = 0; i < m_images.size(); i++) {
      std::vector<std::string> local_images;
      local_images.push_back(m_images[i]);
      m_chooseFiles = NULL;
      MainWidget * widget = new MainWidget(centralWidget,
                                           m_opt,
                                           i, m_output_prefix,
                                           local_images, m_matches,
                                           m_chooseFiles,
                                           m_use_georef, m_hillshade);

      m_widgets.push_back(widget);
    }
  }

  // Put the images in a grid
  int num_widgets = m_widgets.size();
  QGridLayout *grid = new QGridLayout(centralWidget);
  for (int i = 0; i < num_widgets; i++) {
    // Add the current widget
    int row = i / m_grid_cols;
    int col = i % m_grid_cols;
    grid->addWidget(m_widgets[i], row, col);

    // Intercept this widget's request to view (or refresh) the matches in all
    // the widgets, not just this one's.
    connect(m_widgets[i], SIGNAL(refreshAllMatches()), this, SLOT(viewMatches()));
  }
  QWidget *container = new QWidget(centralWidget);
  container->setLayout(grid);
  splitter->addWidget(container);

  // Set new layout
  QGridLayout *layout = new QGridLayout(centralWidget);
  layout->addWidget (splitter, 0, 0, 0);
  centralWidget->setLayout(layout);
}

void MainWindow::createMenus() {

  QMenuBar* menu = menuBar();

  // Exit or Quit
  m_exit_action = new QAction(tr("Exit"), this);
  m_exit_action->setShortcut(tr("Q"));
  m_exit_action->setStatusTip(tr("Exit the application"));
  connect(m_exit_action, SIGNAL(triggered()), this, SLOT(forceQuit()));

  // Run stereo
  m_run_stereo_action = new QAction(tr("Run stereo"), this);
  m_run_stereo_action->setStatusTip(tr("Run stereo on selected clips."));
  connect(m_run_stereo_action, SIGNAL(triggered()), this, SLOT(run_stereo()));
  m_run_stereo_action->setShortcut(tr("R"));

  // Run parallel_stereo
  m_run_parallel_stereo_action = new QAction(tr("Run parallel_stereo"), this);
  m_run_parallel_stereo_action->setStatusTip(tr("Run parallel_stereo on selected clips."));
  connect(m_run_parallel_stereo_action, SIGNAL(triggered()), this, SLOT(run_parallel_stereo()));

  // Size to fit
  m_sizeToFit_action = new QAction(tr("Size to fit"), this);
  m_sizeToFit_action->setStatusTip(tr("Change the view to encompass the images."));
  connect(m_sizeToFit_action, SIGNAL(triggered()), this, SLOT(sizeToFit()));
  m_sizeToFit_action->setShortcut(tr("F"));

  m_viewSingleWindow_action = new QAction(tr("Single window"), this);
  m_viewSingleWindow_action->setStatusTip(tr("View images in a single window."));
  connect(m_viewSingleWindow_action, SIGNAL(triggered()), this, SLOT(viewSingleWindow()));

  m_viewSideBySide_action = new QAction(tr("Side-by-side"), this);
  m_viewSideBySide_action->setStatusTip(tr("View images side-by-side."));
  connect(m_viewSideBySide_action, SIGNAL(triggered()), this, SLOT(viewSideBySide()));

  m_viewAsTiles_action = new QAction(tr("As tiles on grid"), this);
  m_viewAsTiles_action->setStatusTip(tr("View images as tiles on grid."));
  connect(m_viewAsTiles_action, SIGNAL(triggered()), this, SLOT(viewAsTiles()));

  // View hillshaded images
  m_viewHillshadedImages_action = new QAction(tr("Hillshaded images"), this);
  m_viewHillshadedImages_action->setStatusTip(tr("View hillshaded images."));
  m_viewHillshadedImages_action->setCheckable(true);
  m_viewHillshadedImages_action->setChecked(m_hillshade);
  connect(m_viewHillshadedImages_action, SIGNAL(triggered()), this, SLOT(viewHillshadedImages()));

  // View overlayed
  m_viewOverlayedImages_action = new QAction(tr("Overlay georeferenced images"), this);
  m_viewOverlayedImages_action->setStatusTip(tr("View hillshaded images."));
  m_viewOverlayedImages_action->setCheckable(true);
  connect(m_viewOverlayedImages_action, SIGNAL(triggered()), this, SLOT(viewOverlayedImages()));

  m_viewMatches_action = new QAction(tr("Show IP matches"), this);
  m_viewMatches_action->setStatusTip(tr("View interest point matches."));
  connect(m_viewMatches_action, SIGNAL(triggered()), this, SLOT(viewMatches()));

  m_hideMatches_action = new QAction(tr("Hide IP matches"), this);
  m_hideMatches_action->setStatusTip(tr("Hide interest point matches."));
  connect(m_hideMatches_action, SIGNAL(triggered()), this, SLOT(hideMatches()));

  m_saveMatches_action = new QAction(tr("Save IP matches"), this);
  m_saveMatches_action->setStatusTip(tr("Save interest point matches."));
  connect(m_saveMatches_action, SIGNAL(triggered()), this, SLOT(saveMatches()));

  // Shadow threshold calculation
  m_shadowCalc_action = new QAction(tr("Shadow threshold detection"), this);
  m_shadowCalc_action->setStatusTip(tr("Shadow threshold detection."));
  m_shadowCalc_action->setCheckable(true);
  connect(m_shadowCalc_action, SIGNAL(triggered()), this, SLOT(shadowThresholdCalc()));

  // Shadow threshold visualization
  m_viewThreshImages_action = new QAction(tr("View shadow-thresholded images"), this);
  m_viewThreshImages_action->setStatusTip(tr("View shadow-thresholded images."));
  connect(m_viewThreshImages_action, SIGNAL(triggered()), this, SLOT(viewThreshImages()));

  // Shadow threshold visualization
  m_viewUnthreshImages_action = new QAction(tr("View un-thresholded images"), this);
  m_viewUnthreshImages_action->setStatusTip(tr("View un-thresholded images."));
  connect(m_viewUnthreshImages_action, SIGNAL(triggered()), this, SLOT(viewUnthreshImages()));

  // The About Box
  m_about_action = new QAction(tr("About stereo_gui"), this);
  m_about_action->setStatusTip(tr("Show the stereo_gui about box."));
  connect(m_about_action, SIGNAL(triggered()), this, SLOT(about()));

  // File menu
  m_file_menu = menu->addMenu(tr("&File"));
  m_file_menu->addAction(m_exit_action);

  // Run menu
  m_file_menu = menu->addMenu(tr("&Run"));
  m_file_menu->addAction(m_run_stereo_action);
  m_file_menu->addAction(m_run_parallel_stereo_action);

  // View menu
  m_view_menu = menu->addMenu(tr("&View"));
  m_view_menu->addAction(m_sizeToFit_action);
  m_view_menu->addAction(m_viewSingleWindow_action);
  m_view_menu->addAction(m_viewSideBySide_action);
  m_view_menu->addAction(m_viewAsTiles_action);
  m_view_menu->addAction(m_viewHillshadedImages_action);
  m_view_menu->addAction(m_viewOverlayedImages_action);

  // Matches menu
  m_matches_menu = menu->addMenu(tr("&IP matches"));
  m_matches_menu->addAction(m_viewMatches_action);
  m_matches_menu->addAction(m_hideMatches_action);
  m_matches_menu->addAction(m_saveMatches_action);

  // Threshold menu
  m_threshold_menu = menu->addMenu(tr("&Threshold"));
  m_threshold_menu->addAction(m_shadowCalc_action);
  m_threshold_menu->addAction(m_viewThreshImages_action);
  m_threshold_menu->addAction(m_viewUnthreshImages_action);

  // Help menu
  m_help_menu = menu->addMenu(tr("&Help"));
  m_help_menu->addAction(m_about_action);
}

void MainWindow::resizeEvent(QResizeEvent *){
  if (m_chooseFiles)
    m_chooseFiles->setMaximumSize(int(m_widRatio*size().width()), size().height());
}

void MainWindow::closeEvent(QCloseEvent *){
  forceQuit();
}

void MainWindow::forceQuit(){
  exit(0); // A fix for an older buggy version of Qt
}

void MainWindow::sizeToFit(){
  for (size_t i = 0; i < m_widgets.size(); i++) {
    if (m_widgets[i])
      m_widgets[i]->sizeToFit();
  }
}

void MainWindow::viewSingleWindow(){
  m_grid_cols = std::numeric_limits<int>::max();
  m_view_type = VIEW_IN_SINGLE_WINDOW;
  createLayout();
}

void MainWindow::viewSideBySide(){
  m_grid_cols = std::numeric_limits<int>::max();
  m_view_type = VIEW_SIDE_BY_SIDE;
  createLayout();
}

void MainWindow::viewAsTiles(){

  std::string gridColsStr;
  bool ans = getStringFromGui("Number of columns in the grid",
                              "Number of columns in the grid",
                              "",
                              gridColsStr);
  if (!ans)
    return;

  m_grid_cols = atoi(gridColsStr.c_str());
  m_view_type = VIEW_AS_TILES_ON_GRID;
  createLayout();
}

void MainWindow::viewMatches(){

  // We will load the matches just once, as we later will add/delete matches manually
  if (!m_matches_were_loaded && (!m_matches.empty()) && m_matches[0].empty()) {

    if (m_output_prefix == "" ) {
      popUp("Output prefix was not set. Cannot view matches.");
      return;
    }

    m_matches_were_loaded = true;
    m_matches.clear();
    m_matches.resize(m_images.size());

    // All images must have the same number of matches to be able to view them
    int num_matches = -1;

    for (int i = 0; i < int(m_images.size()); i++) {
      for (int j = int(i+1); j < int(m_images.size()); j++) {
        std::string match_filename
          = vw::ip::match_filename(m_output_prefix, m_images[i], m_images[j]);
        vw_out() << "Loading " << match_filename << std::endl;
        try {
          ip::read_binary_match_file( match_filename, m_matches[i], m_matches[j]);

          if (num_matches < 0)
            num_matches = m_matches[i].size();

          if (num_matches != int(m_matches[i].size()) ||
              num_matches != int(m_matches[j].size()) ) {
            m_matches.clear();
            m_matches.resize(m_images.size());
            popUp(std::string("All image must have the same number of matching ")
                  + "interest point to be able to display them.");
            return;
          }

        }catch(...){
          m_matches.clear();
          m_matches.resize(m_images.size());
          popUp("Could not read matches file: " + match_filename);
          return;
        }
      }
    }

    if (m_matches.empty() || m_matches[0].empty()) {
      popUp("Could not load any matches");
      return;
    }
  }

  bool hide = false;
  for (size_t i = 0; i < m_widgets.size(); i++) {
    if (m_widgets[i]) m_widgets[i]->viewMatches(hide);
  }

  m_matches_were_loaded = true;
}

void MainWindow::hideMatches(){
  bool hide = true;
  for (size_t i = 0; i < m_widgets.size(); i++) {
    if (m_widgets[i]) m_widgets[i]->viewMatches(hide);
  }
}

void MainWindow::saveMatches(){

  if (m_output_prefix == "") {
    popUp("Output prefix was not set. Cannot save matches.");
    return;
  }

  // Sanity check
  for (int i = 0; i < int(m_matches.size()); i++) {
    if (m_matches[0].size() != m_matches[i].size()) {
      popUp("Cannot save matches. Must have the same number of matches in each image.");
      return;
    }
  }

  for (int i = 0; i < int(m_images.size()); i++) {
    for (int j = int(i+1); j < int(m_images.size()); j++) {
      std::string match_filename
        = vw::ip::match_filename(m_output_prefix, m_images[i], m_images[j]);
      try {
        vw_out() << "Writing: " << match_filename << std::endl;
        ip::write_binary_match_file(match_filename, m_matches[i], m_matches[j]);
      }catch(...){
        popUp("Failed to save matches.");
      }
    }
  }

  m_matches_were_loaded = true;
}


void MainWindow::run_stereo_or_parallel_stereo(std::string const& cmd){

  if (m_widgets.size() != 2) {
    QMessageBox::about(this, tr("Error"), tr("Need to have two images side-by-side to run stereo."));
    return;
  }

  QRect left_win, right_win;
  if (!m_widgets[0]->get_crop_win(left_win))
    return;
  if (!m_widgets[1]->get_crop_win(right_win))
    return;

  int left_x = left_win.x();
  int left_y = left_win.y();
  int left_wx = left_win.width();
  int left_wy = left_win.height();

  int right_x = right_win.x();
  int right_y = right_win.y();
  int right_wx = right_win.width();
  int right_wy = right_win.height();

  // Command
  std::string run_cmd = cmd + " ";
  for (int i = 1; i < m_argc; i++) {
    run_cmd += std::string(m_argv[i]) + " ";
  }
  std::ostringstream os;
  os << "--left-image-crop-win " << left_x << " " << left_y << " "
     << left_wx << " " << left_wy << " ";
  os << "--right-image-crop-win " << right_x << " " << right_y << " "
     << right_wx << " " << right_wy << " ";
  run_cmd += os.str();
  vw_out() << "Running: " << run_cmd << std::endl;
  system(run_cmd.c_str());
  QMessageBox::about(this, tr("Error"), tr("Done running stereo"));

}

void MainWindow::run_stereo(){
  MainWindow::run_stereo_or_parallel_stereo("stereo");
}

void MainWindow::run_parallel_stereo(){
  MainWindow::run_stereo_or_parallel_stereo("parallel_stereo");
}

// Toggle on or of the tool for detecting the shadow threshold in images
void MainWindow::shadowThresholdCalc() {
  bool on = m_shadowCalc_action->isChecked();
  for (size_t i = 0; i < m_widgets.size(); i++) {
    if (m_widgets[i])
      m_widgets[i]->setShadowThreshMode(on);
  }

}

void MainWindow::viewThreshImages() {
  for (size_t i = 0; i < m_widgets.size(); i++) {
    if (m_widgets[i]) {
      m_widgets[i]->viewThreshImages();
    }
  }
}

void MainWindow::viewUnthreshImages() {
  for (size_t i = 0; i < m_widgets.size(); i++) {
    if (m_widgets[i]) {
      m_widgets[i]->viewUnthreshImages();
    }
  }
}

void MainWindow::viewHillshadedImages() {
  bool on = m_viewHillshadedImages_action->isChecked();
  for (size_t i = 0; i < m_widgets.size(); i++) {
    if (m_widgets[i]) {
      m_widgets[i]->viewHillshadedImages(on);
    }
  }
}

void MainWindow::viewOverlayedImages() {
  bool on = m_viewOverlayedImages_action->isChecked();
  if (on) {

    // Will show in single window with georef. Must first check if all images
    // have georef.
    for (size_t i = 0; i < m_images.size(); i++) {
      cartography::GeoReference georef;
      bool has_georef = vw::cartography::read_georeference(georef, m_images[i]);
      if (!has_georef) {
        popUp("Cannot overlay, as there is no georeference in: " + m_images[i]);
        return;
      }
    }

    m_view_type_old = m_view_type; // back this up
    m_view_type = VIEW_IN_SINGLE_WINDOW;
    m_use_georef = true;
  }else{
    m_use_georef = false;
    m_view_type = m_view_type_old; // restore this
  }

  createLayout();
}

void MainWindow::about() {
  std::ostringstream about_text;
  about_text << "<h3>stereo_gui</h3>"
             << "<p>Copyright &copy; 2015 NASA Ames Research Center. See the manual for documentation.</p>";
  QMessageBox::about(this, tr("About stereo_gui"),
                     tr(about_text.str().c_str()));

}

void MainWindow::keyPressEvent(QKeyEvent *event) {

  std::ostringstream s;

  switch (event->key()) {
  case Qt::Key_Escape:  // Quit
    close();
    break;
  }
}

bool MainWindow::getStringFromGui(std::string title, std::string description,
                                  std::string inputStr,
                                  std::string & outputStr // output
                                  ){
  outputStr = "";

  bool ok = false;
  QString text = QInputDialog::getText(this, title.c_str(), description.c_str(),
                                       QLineEdit::Normal, inputStr.c_str(),
                                       &ok);

  if (ok) outputStr = text.toStdString();

  return ok;
}
