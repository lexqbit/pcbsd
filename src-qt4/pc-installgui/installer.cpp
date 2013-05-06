#include <QCloseEvent>
#include <QProcess>
#include <QTimer>
#include <QTranslator>
#include <QGraphicsPixmapItem>

#include "backend.h"
#include "installer.h"
#include "helpText.h"

Installer::Installer(QWidget *parent) : QMainWindow(parent)
{
    setupUi(this);
    translator = new QTranslator();
    haveWarnedSpace=false;

    connect(abortButton, SIGNAL(clicked()), this, SLOT(slotAbort()));
    connect(backButton, SIGNAL(clicked()), this, SLOT(slotBack()));
    connect(nextButton, SIGNAL(clicked()), this, SLOT(slotNext()));

    connect(helpButton, SIGNAL(clicked()), this, SLOT(slotHelp()));
    connect(pushTouchKeyboard, SIGNAL(clicked()), this, SLOT(slotPushVirtKeyboard()));
    connect(pushChangeKeyLayout, SIGNAL(clicked()), this, SLOT(slotPushKeyLayout()));
    connect(pushHardware, SIGNAL(clicked()), this, SLOT(slotCheckHardware()));

    abortButton->setText(tr("&Cancel"));
    backButton->setText(tr("&Back"));
    nextButton->setText(tr("&Next"));

    // Init the MBR to yes
    loadMBR = true;
    // Init the GPT to no
    loadGPT = false;

    // No optional components by default
    fSRC=false;
    fPORTS=false;

    // Load the keyboard info
    keyModels = Scripts::Backend::keyModels();
    keyLayouts = Scripts::Backend::keyLayouts();

    // Set the arch name
    setArch();

    // Start on the first screen
    installStackWidget->setCurrentIndex(0);
    backButton->setVisible(false);
}

Installer::~Installer()
{
    //delete ui;
}

bool Installer::isInstalled()
{
    return false;
}

void Installer::setArch()
{
   QProcess m;
   m.start(QString("uname"), QStringList() << "-m");
   while(m.state() == QProcess::Starting || m.state() == QProcess::Running) {
      m.waitForFinished(200);
      QCoreApplication::processEvents();
   }

   // Get output
   Arch = m.readLine().simplified();
   qDebug() << Arch;
}

void Installer::slotCheckHardware()
{
  pcHardware = new dialogCheckHardware();
  pcHardware->programInit();
  pcHardware->setWindowModality(Qt::ApplicationModal);
  pcHardware->show();
  pcHardware->raise();
}

void Installer::slotPushKeyLayout()
{
  wKey = new widgetKeyboard();
  wKey->programInit(keyModels, keyLayouts);
  wKey->setWindowModality(Qt::ApplicationModal);
  wKey->show();
  wKey->raise();
}

void Installer::initInstall()
{
    // load languages
    comboLanguage->clear();
    languages = Scripts::Backend::languages();
    for (int i=0; i < languages.count(); ++i) {
        QString languageStr = languages.at(i);
        QString language = languageStr.split("-").at(0);
        comboLanguage->addItem(language.trimmed());
    }
    connect(comboLanguage, SIGNAL(currentIndexChanged(QString)), this, SLOT(slotChangeLanguage()));

    // Load any package scheme data
    listDeskPkgs = Scripts::Backend::getPackageData(availDesktopPackageData, QString());
    listServerPkgs = Scripts::Backend::getPackageData(availServerPackageData, QString("trueos"));

    // Do check for available meta-pkgs on boot media
    if ( QFile::exists("/tmp/no-meta-pkgs") )
    	hasPkgsOnMedia = false;
    else
    	hasPkgsOnMedia = true;

    qDebug() << "Package Media:" << availDesktopPackageData << hasPkgsOnMedia;

    // See if this media has a freebsd release on it
    if ( QFile::exists("/tmp/no-fbsd-release") ) {
	hasFreeBSDOnMedia = false;
    } else {
	hasFreeBSDOnMedia = true;
    }
	
    // Do check for install pkgs on boot media
    if ( QFile::exists("/tmp/no-install-pkgs") ) {
    	hasInstallOnMedia = false;
    } else {
    	hasInstallOnMedia = true;
    }

    // Set the key toggle
    toggleKeyLayout = true;

    // Is this a LIVE disk?
    if ( QFile::exists("/usr/pcbsd-live") )
    	isLiveMode = true;
    else
    	isLiveMode = false;

    // Get available memory
    systemMemory = Scripts::Backend::systemMemory();

    // Load up the keyboard information
    //connectKeyboardSlots();

    // Connect the disk slots
    connect(pushDiskCustomize,SIGNAL(clicked()), this, SLOT(slotDiskCustomizeClicked()));

    // Load the disks
    loadDiskInfo();
    

    // Init the desktop wheel
    initDesktopSelector();
}

void Installer::loadDiskInfo()
{
   sysDisks = Scripts::Backend::hardDrives();
   if ( sysDisks.empty() ) {
      QMessageBox::critical(this, tr("PC-BSD Installer"),
                                tr("Unable to detect any disk drives! The install will now exit."),
                                QMessageBox::Ok,
                                QMessageBox::Ok);
      exit(1);
   }

   // For now use the first disk we find
   if ( ! autoGenPartitionLayout(sysDisks.at(0).at(1), true) )
   {
      QMessageBox::critical(this, tr("PC-BSD Installer"),
        tr("Unable to suggest a partition for the detected disk."),
        QMessageBox::Ok,
        QMessageBox::Ok);
   }

   textEditDiskSummary->clear();
   QStringList summary = getDiskSummary();
   for ( int i=0; i < summary.count(); ++i)
     textEditDiskSummary->append(summary.at(i));

   textEditDiskSummary->moveCursor(QTextCursor::Start);

}

// Function which will auto-generate a partition layout based upon the target disk / slice
bool Installer::autoGenPartitionLayout(QString target, bool isDisk)
{
  QString targetType, tmp;
  int targetLoc, totalSize = 0, mntsize;
  QString targetDisk, targetSlice, tmpPass, fsType;
  bool ok;
  ok = false;

  // Clear out the original disk layout
  sysFinalDiskLayout.clear();
  QStringList fileSystem;
  qDebug() << "Generating disk layout";


  if ( isDisk ) {
    targetType = "DRIVE";
    targetSlice = "ALL";
    targetDisk = target;
    targetLoc = 1;
  } else {
    targetType = "SLICE";
    targetDisk = target;
    targetDisk.truncate(targetDisk.size() -2);
    targetSlice = target;
    targetSlice = targetSlice.remove(0, targetSlice.size() -2);
    targetLoc = 2;
  }
  
  // Lets get the size for this disk / partition
  for (int i=0; i < sysDisks.count(); ++i) {
      // Make sure to only add the slices to the listDiskSlices
      if ( sysDisks.at(i).at(0) == targetType && target == sysDisks.at(i).at(targetLoc))
        totalSize = sysDisks.at(i).at(targetLoc + 1).toInt(&ok);
  }

  // Give us a small buffer for rounding errors
  totalSize = totalSize - 10;

  // We got a valid size for this disk / slice, lets generate the layout now
  if( !ok )
    return false;


  // If on amd64 lets use ZFS, it rox
  if ( Arch == "amd64" ) {
    // Add the main zfs pool with standard partitions
    fsType= "ZFS";
    fileSystem << targetDisk << targetSlice << "/,/tmp(compress=lzjb),/usr(canmount=off),/usr/home,/usr/jails,/usr/obj(compress=lzjb),/usr/pbi,/usr/ports(compress=gzip),/usr/ports/distfiles(compress=off),/usr/src(compress=gzip),/var(canmount=off),/var/audit(compress=lzjb),/var/log(compress=gzip),/var/tmp(compress=lzjb)" << fsType << tmp.setNum(totalSize) << "" << "";
    //qDebug() << "Auto-Gen FS:" <<  fileSystem;
    sysFinalDiskLayout << fileSystem;
    fileSystem.clear();
    return true;
  }

  // Looks like not on amd64, fallback to UFS+SUJ and print a nice 
  // warning for the user explaining they *really* want to be on amd64
  QMessageBox::warning(this, tr("PC-BSD Installer"),
      tr("Detected that you are running the 32bit version. If your system is 64bit capable (most systems made after 2005), you really should be running the 64bit version"),
      QMessageBox::Ok,
      QMessageBox::Ok);

  // Start the UFS layout now
  mntsize = 2000;
  fsType="UFS+SUJ";

  fileSystem << targetDisk << targetSlice << "/" << fsType << tmp.setNum(mntsize) << "" << "";
  totalSize = totalSize - mntsize;
  //qDebug() << "Auto-Gen FS:" <<  fileSystem;
  sysFinalDiskLayout << fileSystem;
  fileSystem.clear();
    

  // Figure out the swap size, try for 2xPhysMem first, fallback to 256 if not enough space
  mntsize = systemMemory * 2;
  if ( totalSize - mntsize < 3000 )
     mntsize = 256;

  // Cap the swap size to 2GB
  if ( mntsize > 2000 )
     mntsize = 2000;

  fileSystem << targetDisk << targetSlice << "SWAP" << "SWAP" << tmp.setNum(mntsize) << "" << "";
  totalSize = totalSize - mntsize;
  //qDebug() << "Auto-Gen FS:" <<  fileSystem;
  sysFinalDiskLayout << fileSystem;
  fileSystem.clear();

  // If less than 3GB, skip /var and leave on /
  if ( totalSize > 3000 ) {
    // Figure out the default size for /var if we are on FreeBSD / PC-BSD
    mntsize = 2048;
    fileSystem << targetDisk << targetSlice << "/var" << fsType << tmp.setNum(mntsize) << "" << "";
    totalSize = totalSize - mntsize;
    //qDebug() << "Auto-Gen FS:" <<  fileSystem;
    sysFinalDiskLayout << fileSystem;
    fileSystem.clear();
  }

  // Now use the rest of the disk / slice for /usr
  fileSystem << targetDisk << targetSlice << "/usr" << fsType << tmp.setNum(totalSize) << "" << "";
  //qDebug() << "Auto-Gen FS:" <<  fileSystem;
  sysFinalDiskLayout << fileSystem;
  fileSystem.clear();

  return true; 
  
}

// Function which returns the pc-sysinstall cfg data
QStringList Installer::getDiskSummary()
{
  QList<QStringList> copyList;
  QStringList summaryList;
  QString tmp, workingDisk, workingSlice, tmpSlice, XtraTmp, startPart, sliceSize;
  int disk = 0;

  // Copy over the list to a new variable we can mangle without modifying the original
  copyList = sysFinalDiskLayout;

  if ( copyList.at(0).at(0) == "MANUAL" )
  {
    summaryList << "";
    summaryList << tr("Installing to file-system mounted at /mnt");
    return summaryList;
  }

  // Start our summary
  summaryList << "";
  summaryList << "<b>" + tr("The disk will be setup with the following configuration:") + "</b>";

  while ( ! copyList.empty() )
  {
    workingDisk = copyList.at(0).at(0);
    workingSlice = copyList.at(0).at(1);
    tmpSlice = workingSlice;

    // Check if this is an install to "Unused Space"
    for (int z=0; z < sysDisks.count(); ++z)
      if ( sysDisks.at(z).at(0) == "SLICE" \
        && sysDisks.at(z).at(2) == workingDisk + workingSlice \
        && sysDisks.at(z).at(4) == "Unused Space" )
          tmpSlice = "free";

    // Check for any mirror for this device
    for (int i=0; i < copyList.count(); ++i) {
       if ( copyList.at(i).at(2).indexOf("MIRROR(" + workingDisk + ")") != -1 )
       {
         summaryList << tr("Disk:") + copyList.at(i).at(0) + " " + tr("Mirroring:") + workingDisk;
         copyList.removeAt(i);
         break;
       }
    }

    // If after doing the mirror, our list is empty, break out
    if ( copyList.empty() )
      break;
    
    // If there is a dedicated /boot partition, need to list that first, see what is found
    for (int i=0; i < copyList.count(); ++i) {
      QStringList mounts = copyList.at(i).at(2).split(",");
      for (int z = 0; z < mounts.size(); ++z) {
        if ( copyList.at(i).at(0) == workingDisk \
          && copyList.at(i).at(1) == workingSlice \
          && mounts.at(z) == "/boot" )
		startPart="/boot";
      }
    }

    // If no dedicated /boot partition, then lets list "/" first
    if(startPart.isEmpty())
	startPart="/";

    // Start by looking for the root partition
    for (int i=0; i < copyList.count(); ++i) {
      QStringList mounts = copyList.at(i).at(2).split(",");
      for (int z = 0; z < mounts.size(); ++z) {
        if ( copyList.at(i).at(0) == workingDisk \
          && copyList.at(i).at(1) == workingSlice \
          && mounts.at(z) == startPart ) {

          // Check if we have any extra arguments to throw on the end
          XtraTmp="";
          if ( ! copyList.at(i).at(5).isEmpty() )
            XtraTmp=" (" + copyList.at(i).at(5) + ")" ;

          // Write the user summary
          summaryList << "";
          summaryList << tr("Partition:") + " " + workingDisk + "(" + workingSlice + "):";
          summaryList << tr("FileSystem:") + " " + copyList.at(i).at(3);
          summaryList << tr("Size:") + " " + copyList.at(i).at(4) + "MB ";
          if ( copyList.at(i).at(3) == "ZFS" ) {
            QStringList zDS = copyList.at(i).at(2).split(",/");
            QString zTMP;
            for (int ds = 0; ds < zDS.size(); ++ds) {
              if ( zDS.at(ds) != "/" )
                zDS.replace(ds, "/" + zDS.at(ds));
              if ( zDS.at(ds).indexOf("(") != -1 ) {
                zTMP = zDS.at(ds);
                zTMP.replace("(", " (");
                zDS.replace(ds, zTMP );
              }
            } 
            summaryList << tr("ZFS Datasets:<br>") + " " + zDS.join("<br>");
          } else {
            summaryList << tr("Mount:") + " " + copyList.at(i).at(2);
          }
          if ( ! XtraTmp.isEmpty() ) {
            summaryList << tr("Options:") + " " + copyList.at(i).at(5);
          }

          // Done with this item, remove it now
          copyList.removeAt(i);
          break;
        }
      }
    }


    // Now look for SWAP
    for (int i=0; i < copyList.count(); ++i) {
      if ( copyList.at(i).at(0) == workingDisk \
        && copyList.at(i).at(1) == workingSlice \
        && copyList.at(i).at(2) == "SWAP" ) {

        // Write the user summary
        summaryList << "";
        summaryList << tr("Partition:") + " " + workingDisk + "(" + workingSlice + "):";
        summaryList << tr("FileSystem:") + " " + copyList.at(i).at(3);
        summaryList << tr("Size:") + " " + copyList.at(i).at(4) + "MB ";

        // Done with this item, remove it now
        copyList.removeAt(i);
        break;
      }
    }

 
    // Now look for any other partitions
    int count = copyList.count();
    for (int i=0; i < count; ++i) {
      if ( copyList.at(i).at(0) == workingDisk \
        && copyList.at(i).at(1) == workingSlice ) {

        // Check if we have any extra arguments to throw on the end
        XtraTmp="";
        if ( ! copyList.at(i).at(5).isEmpty() )
          XtraTmp=" (" + copyList.at(i).at(5) + ")" ;

	// If we are working on the last partition, set the size to 0 to use remaining disk
	if ( i == (count - 1) ) 
		sliceSize = "0";
	else
		sliceSize=copyList.at(i).at(4);

        // Write the user summary
        summaryList << "";
        summaryList << tr("Partition:") + " " + workingDisk + "(" + workingSlice + "):";
        summaryList << tr("FileSystem:") + " " + copyList.at(i).at(3);
        summaryList << tr("Size:") + " " + copyList.at(i).at(4) + "MB ";
	if ( copyList.at(i).at(3) != "ZFS" )
          summaryList << tr("Mount:") + " " + copyList.at(i).at(2);
        if ( ! XtraTmp.isEmpty() ) {
          summaryList << tr("Options:") + " " + copyList.at(i).at(5);
        }

        // Done with this item, remove it now
        copyList.removeAt(i);
        i--;
        count--;
      }
    }

    // Increment our disk counter
    disk++;
  }

  return summaryList;
}

void Installer::slotDiskCustomizeClicked()
{
  wDisk = new wizardDisk();
  wDisk->programInit();
  wDisk->setWindowModality(Qt::ApplicationModal);
  connect(wDisk, SIGNAL(saved(QList<QStringList>, bool, bool)), this, SLOT(slotSaveDiskChanges(QList<QStringList>, bool, bool)));
  wDisk->show();
  wDisk->raise();
}

void Installer::slotDesktopCustomizeClicked()
{
  desks = new desktopSelection();
  if ( wheelCurItem != wPCSERVER && wheelCurItem != 11 && wheelCurItem != 12)
     desks->programInit(listDeskPkgs,selectedPkgs);
  else
     desks->programInit(listServerPkgs,selectedPkgs);
  desks->setWindowModality(Qt::ApplicationModal);
  connect(desks, SIGNAL(saved(QStringList)), this, SLOT(slotSaveMetaChanges(QStringList)));
  desks->show();
  desks->raise();
}

void Installer::slotSaveMetaChanges(QStringList sPkgs)
{
  selectedPkgs = sPkgs;

  // Only add +10 if we are not already on the custom screen
  if ( wheelCurItem < 10 )
    wheelCurItem= wheelCurItem + 10;

  switch (wheelCurItem) {
    case 12:
        groupDeskSummary->setTitle(tr("TrueOS Package Selection"));
        break;
    case 11:
        groupDeskSummary->setTitle(tr("FreeBSD Package Selection"));
        break;
    default:
        groupDeskSummary->setTitle(tr("PC-BSD Package Selection"));
        break;
  }

  textDeskSummary->setText(tr("The following meta-pkgs will be installed:") + "<br>" + selectedPkgs.join("<br>"));
  graphicsViewOS->setScene(customScene);
}

void Installer::slotSaveDiskChanges(QList<QStringList> newSysDisks, bool MBR, bool GPT)
{
  // Save the new disk layout
  loadMBR = MBR;
  loadGPT = GPT;
  sysFinalDiskLayout = newSysDisks;
  textEditDiskSummary->clear();
  QStringList summary = getDiskSummary();
  for ( int i=0; i < summary.count(); ++i)
    textEditDiskSummary->append(summary.at(i));
               
  textEditDiskSummary->moveCursor(QTextCursor::Start);
 
  // Regenerate the config
  startConfigGen();
}

void Installer::slotDesktopLeftClicked()
{
  if ( wheelCurItem >= 10 ) {
    int ret = QMessageBox::question(this, tr("PC-BSD Installer"),
                              tr("You currently have a custom package set configured. Continue changing to a default set?"),
                              QMessageBox::No | QMessageBox::Yes,
                              QMessageBox::No);
    switch (ret) {
    case QMessageBox::Yes:
        break;
    case QMessageBox::No: // :)
        return;
        break;
    }
    wheelCurItem = wheelCurItem - 10;
    graphicsViewOS->setScene(defaultScene);
  }
  moveDesktopWheel(false);
}

void Installer::slotDesktopRightClicked()
{
  if ( wheelCurItem >= 10 ) {
    int ret = QMessageBox::question(this, tr("PC-BSD Installer"),
                              tr("You currently have a custom package set configured. Continue changing to a default set?"),
                              QMessageBox::No | QMessageBox::Yes,
                              QMessageBox::No);
    switch (ret) {
    case QMessageBox::Yes:
        break;
    case QMessageBox::No: // :)
        return;
        break;
    }
    wheelCurItem = wheelCurItem - 10;
    graphicsViewOS->setScene(defaultScene);
  }
  moveDesktopWheel(true);
}

void Installer::moveDesktopWheel(bool direction)
{
  qDebug() << wheelCurItem << direction;
  // Make sure we aren't scrolling too far
  if ( direction && wheelCurItem >= wheelIcons.size() )
    return;
  if ( ! direction && wheelCurItem <= 1 )
    return;


  int tItem, tPixel, cPixel;
  cPixel=96 + ((wheelCurItem-1) * 64) + (wheelCurItem * 32);

  // Right
  if ( direction ) {
    tItem=wheelCurItem + 1;
    tPixel=96 + ((tItem-1) * 64) + (tItem * 32);
  } else {
  // Left
    tItem=wheelCurItem - 1;
    tPixel=96 + ((tItem-1) * 64) + (tItem * 32);
  } 

  if ( direction ) {
    while ( cPixel < tPixel ) {
      cPixel++;
      graphicsViewOS->centerOn(cPixel,0);
      graphicsViewOS->show();
      QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 22);
      
    }
  } else {
    while ( cPixel > tPixel ) {
      cPixel--;
      graphicsViewOS->centerOn(cPixel,0);
      graphicsViewOS->show();
      QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 22);
    }
  }
  
  wheelCurItem=tItem;

  groupDeskSummary->setTitle(wheelName.at(tItem-1));
  textDeskSummary->setText(wheelDesc.at(tItem-1));

  // No custom packages for FreeBSD vanilla
  if ( wheelCurItem == 1 ) 
    pushDeskCustomize->setEnabled(false);
  else
    pushDeskCustomize->setEnabled(true);
  
  changeMetaPkgSelection();
}

void Installer::changeMetaPkgSelection()
{

  // Set the default desktop meta-pkgs based upon the selection
  // 1 = FreeBSD
  switch (wheelCurItem)
  {
    case wKDE:
      selectedPkgs.clear();
      selectedPkgs << "KDE" << "KDE-Accessibility" << "KDE-Artwork" << "KDE-Education" << "KDE-Games" << "KDE-Graphics" << "KDE-Multimedia" << "KDE-Network" << "KDE-PIM";
      if ( comboLanguage->currentIndex() != 0 ) 
	 selectedPkgs << "KDE-L10N";
      break;
    case wLXDE:
      selectedPkgs.clear();
      selectedPkgs << "LXDE";
      break;
    case wGNOME:
      selectedPkgs.clear();
      selectedPkgs << "GNOME" << "GNOME-Accessibility" << "GNOME-Games" << "GNOME-Net" << "GNOME-Utilities";
      break;
    case wXFCE:
      selectedPkgs.clear();
      selectedPkgs << "XFCE" << "XFCE-Plugins";
      break;
    default:
      selectedPkgs.clear();
      return;
  }

  // Check if we are using NVIDIA driver and include it automatically
  QFile file("/etc/X11/xorg.conf");
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
      return;
          
  QTextStream in(&file);
  while (!in.atEnd()) {
      QString line = in.readLine();
      if ( line.indexOf("nvidia") != -1 ) {
	selectedPkgs << "NVIDIA";
        break;
      }
  }     
  file.close();
  // Done with NVIDIA check

  qDebug() << selectedPkgs;

}

void Installer::initDesktopSelector()
{
    // Init the desktop selector
    wheelIcons << ":modules/images/freebsd.png" << ":/modules/images/pcbsd-server.png" << ":/PCBSD/images/kde.png" << ":/PCBSD/images/lxde.png" << ":/PCBSD/images/gnome.png" << ":/PCBSD/images/xfce.png";
    wheelName << "FreeBSD Server" << "TrueOS" << "KDE" << "LXDE" << "GNOME" << "XFCE"; 
    wheelDesc << tr("FreeBSD is an advanced operating system for modern server, desktop, and embedded computer platforms. FreeBSD's code base has undergone over thirty years of continuous development, improvement, and optimization.") \
    << tr("TrueOS is a console based server running FreeBSD. It includes command-line versions of The Warden jail management, PBI manager, ZFS boot environments (beadm), and other helpful utilities for system administrators.")  \
    << tr("KDE is a full-featured desktop environment, which includes support for 3D desktop effects, multiple desktops, and a variety of built-in tools and utilities for both new and power-desktop users.<br><br>* Recommended for higher-end systems with 2GB of RAM or more *") \
    << tr("LXDE is a lightweight desktop, minimalist in nature, with support for multiple-desktops, a system tray, application menu and more.<br><br>* Recommended for netbooks, or lower-end systems * ") \
    << tr("GNOME is a full-featured desktop environment, complete with a large number of integrated utilities and tools for desktop users.") \
    << tr("XFCE is a light and modular desktop, with a number of features to enhance customizing the desktop to your liking."); 

    int xOff=96;
    defaultScene = new QGraphicsScene(0,0,(96 + 96 + (wheelIcons.size()*64) + (wheelIcons.size()*32) ),64);
    for ( int i = 0; i < wheelIcons.size(); ++i) {
      defaultScene->addPixmap(QPixmap(wheelIcons.at(i)))->setOffset(xOff,0);
      xOff = xOff +96;
    }
    graphicsViewOS->setScene(defaultScene);

    // If less than 2GB memory, default to LXDE, otherwise KDE
    if ( systemMemory > 2048 )  {
      wheelCurItem=2;
    } else {
      wheelCurItem=3;
    }
    graphicsViewOS->centerOn(191,0);
    graphicsViewOS->show();
    moveDesktopWheel(true);

    customScene = new QGraphicsScene(0,0,220,64);
    customScene->addText(tr("Custom Package Selection"));

    // Connect our slots
    connect(pushDeskRight,SIGNAL(clicked()), this, SLOT(slotDesktopRightClicked()));
    connect(pushDeskLeft,SIGNAL(clicked()), this, SLOT(slotDesktopLeftClicked()));
    connect(pushDeskCustomize,SIGNAL(clicked()), this, SLOT(slotDesktopCustomizeClicked()));
}

void Installer::proceed(bool forward)
{
    int count = installStackWidget->count() - 1;
    int index = installStackWidget->currentIndex();

    index = forward ?
            (index == count ? count : index + 1) :
            (index == 0 ? 0 : index - 1);

    if ( index > 0 && index != 4)
      backButton->setVisible(true);
    else
      backButton->setVisible(false);

    installStackWidget->setCurrentIndex(index);
}

// Slot which is called when the Finish button is clicked
void Installer::slotFinished()
{
  qApp->quit();
}

void Installer::slotSaveFBSDSettings(QString rootPW, QString name, QString userName, QString userPW, QString shell, QString hostname, bool ssh, bool src, bool ports, QStringList netSettings)
{
  fRootPW = rootPW;
  fName = name;
  fUserName = userName;
  fUserPW = userPW;
  fShell = shell;
  fHost = hostname;
  fSSH = ssh;
  fSRC = src;
  fPORTS = ports;
  fNetSettings = netSettings;
  installStackWidget->setCurrentIndex(installStackWidget->currentIndex() + 1);

  // Generate the pc-sysinstall config
  startConfigGen();
}

void Installer::slotNext()
{
   QString tmp;

   // If no pkgs on media
   if ( installStackWidget->currentIndex() == 0 && ! hasPkgsOnMedia) {
     installStackWidget->setCurrentIndex(2);
     return;
   }

   // Start the FreeBSD wizard
   if ( installStackWidget->currentIndex() == 1 && (wheelCurItem == wFREEBSD || wheelCurItem == wPCSERVER || wheelCurItem == 12) ) {
     bool tOS;
     if ( wheelCurItem == wPCSERVER || wheelCurItem == 12 )
       tOS = true;
     else
       tOS = false;

     wFBSD = new wizardFreeBSD();
     wFBSD->setWindowModality(Qt::ApplicationModal);
     wFBSD->programInit(tOS);
     connect(wFBSD, SIGNAL(saved(QString, QString, QString, QString, QString, QString, bool, bool, bool, QStringList)), this, SLOT(slotSaveFBSDSettings(QString, QString, QString, QString, QString, QString, bool, bool, bool, QStringList)));
     wFBSD->show();
     wFBSD->raise();
     return ;
   }

   // Create the pc-sysinstall config
   if ( installStackWidget->currentIndex() == 1 )
     startConfigGen();

   // If the chosen disk is too small or partition is invalid, don't continue
   if ( installStackWidget->currentIndex() == 2 && ! checkDiskRequirements())
      return;

   if ( installStackWidget->currentIndex() == 2 )
   {
      startConfigGen();
      int ret = QMessageBox::question(this, tr("PC-BSD Installer"),
                                tr("Start the installation now?"),
                                QMessageBox::No | QMessageBox::Yes,
                                QMessageBox::No);
      switch (ret) {
      case QMessageBox::Yes:
          startInstall();
          break;
      case QMessageBox::No: // :)
          return;
          break;
      }
   }

   proceed(true);
}

void Installer::slotBack()
{
   // If no pkgs on media
   if ( installStackWidget->currentIndex() == 2 && ! hasPkgsOnMedia) {
	installStackWidget->setCurrentIndex(0);
        return;
   }

   proceed(false);
}

void Installer::slotAbort()
{
   close();
}

void Installer::slotChangeLanguage()
{
    if ( comboLanguage->currentIndex() == -1 )
      return;

    // Figure out the language code
    QString langCode = languages.at(comboLanguage->currentIndex());
    
    // Grab the language code
    langCode.truncate(langCode.lastIndexOf(")"));
    langCode.remove(0, langCode.lastIndexOf("(") + 1); 

    // Check what directory our app is in
    QString appDir;
    if ( QFile::exists("/usr/local/bin/pc-sysinstaller") )
      appDir = "/usr/local/share/pcbsd";
    else
      appDir = QCoreApplication::applicationDirPath();

    //QTranslator *translator = new QTranslator();
    qDebug() << "Remove the translator";
    if ( ! translator->isEmpty() )
      QCoreApplication::removeTranslator(translator);

    if (translator->load( QString("SysInstaller_") + langCode, appDir + "/i18n/" )) {
      qDebug() << "Load new Translator" << langCode;
      QCoreApplication::installTranslator(translator);
      this->retranslateUi(this);
    }
}

void Installer::changeLang(QString code)
{
   // Change the language in the combobox with the current running one
   comboLanguage->disconnect();

   for (int i=0; i < languages.count(); ++i) {
      if ( languages.at(i).indexOf("(" + code + ")" ) != -1 ) {
        comboLanguage->setCurrentIndex(i); 
      }
   }

   connect(comboLanguage, SIGNAL(currentIndexChanged(QString)), this, SLOT(slotChangeLanguage()));
}

QStringList Installer::getGlobalCfgSettings()
{

  QStringList tmpList;
  QString tmp, upDrive;

  tmpList << "# Auto-Generated pc-sysinstall configuration";
  tmpList << "installInteractive=no";

  if ( sysFinalDiskLayout.at(0).at(0) == "MANUAL" )
  {
    tmpList << "installMode=extract";
    tmpList << "installLocation=/mnt";
  } else {
    // Doing a fresh install
    tmpList << "installMode=fresh";
  }
  
  QString distFiles;
  distFiles="base doc games kernel";
  if ( Arch == "amd64" )
     distFiles+=" lib32";

  // If we are doing a PC-BSD install
  if ( wheelCurItem != wPCSERVER && wheelCurItem != 12 && wheelCurItem != wFREEBSD ) {
    tmpList << "installType=PCBSD";
    tmpList << "packageType=dist";
  } else {
    tmpList << "installType=FreeBSD";
    tmpList << "packageType=dist";
  }

  // Set the distFiles being used
  tmpList << "distFiles=" + distFiles;
  tmpList << "";

  // If we have a custom hostname, add it to the config
  if ( ! fHost.isEmpty() )
    tmpList << "hostname=" + fHost;

  
  // Networking setup
  if ( wheelCurItem != wFREEBSD && wheelCurItem != wPCSERVER && wheelCurItem != 12 ) {
    // PC-BSD network setup
    tmpList << "netSaveDev=AUTO-DHCP-SLAAC";
  } else {
    // FreeBSD network setup
    if ( fNetSettings.at(0) == "AUTO-DHCP" )
      tmpList << "netSaveDev=AUTO-DHCP";
    else if ( fNetSettings.at(0) == "AUTO-DHCP-SLAAC" ) {
      tmpList << "netSaveDev=AUTO-DHCP-SLAAC";
      // We cannot rely on SLAAC to provide DNS for example.  The same is true
      // for DHCP but the worls seems to have forgotten...
      tmpList << "netSaveIPv6=" + fNetSettings.at(5);
      tmpList << "netSaveIPv6NameServer=" + fNetSettings.at(6);
      tmpList << "netSaveIPv6DefaultRouter=" + fNetSettings.at(7);
    } else if ( fNetSettings.at(0) == "IPv6-SLAAC" ) {
       tmpList << "netSaveDev=IPv6-SLAAC";
      // We cannot rely on SLAAC to provide DNS for example.  The same is true
      // for DHCP but the worls seems to have forgotten...
      tmpList << "netSaveIPv6=" + fNetSettings.at(5);
      tmpList << "netSaveIPv6NameServer=" + fNetSettings.at(6);
      tmpList << "netSaveIPv6DefaultRouter=" + fNetSettings.at(7);
    }  
    else
    {
      tmp = fNetSettings.at(0);
      if ( tmp.indexOf(":") > 0 )
        tmp.truncate(tmp.indexOf(":"));
      tmpList << "netSaveDev=" + tmp;
      tmpList << "netSaveIP_" + tmp + "=" + fNetSettings.at(1); 
      tmpList << "netSaveMask_" + tmp + "=" + fNetSettings.at(2);
      tmpList << "netSaveNameServer=" + fNetSettings.at(3);
      tmpList << "netSaveDefaultRouter=" + fNetSettings.at(4);
      tmpList << "netSaveIPv6=" + fNetSettings.at(5);
      tmpList << "netSaveIPv6NameServer=" + fNetSettings.at(6);
      tmpList << "netSaveIPv6DefaultRouter=" + fNetSettings.at(7);
    }
  }


  // Doing install from /dist directory
  tmpList << "installMedium=local"; 
  tmpList << "localPath=/dist";

  if ( comboLanguage->currentIndex() != 0 ) {
    QString lang = languages.at(comboLanguage->currentIndex());
    // Grab the language code
    lang.truncate(lang.lastIndexOf(")"));
    lang.remove(0, lang.lastIndexOf("(") + 1);
    tmpList << "";
    tmpList << "localizeLang=" + lang;
  }

  // Setup custom keyboard layouts
  /* KPM
  tmpList << "";
  tmpList << "# Keyboard Layout Options";
  tmp = comboBoxKeyboardModel->currentText();
  tmp.truncate(tmp.indexOf(")"));
  tmp = tmp.remove(0, tmp.indexOf(" (") + 2 );
  tmpList << "localizeKeyModel=" + tmp;

  tmp = listKbLayouts->currentItem()->text();
  tmp.truncate(tmp.indexOf(")"));
  tmp = tmp.remove(0, tmp.indexOf(" (") + 2 );
  tmpList << "localizeKeyLayout=" + tmp;

  tmp = listKbVariants->currentItem()->text();
  if ( tmp != "<none>" ) {
    tmp.truncate(tmp.indexOf(")"));
    tmp = tmp.remove(0, tmp.indexOf(" (") + 2 );
    tmpList << "localizeKeyVariant=" + tmp;
  } 
  */

  tmpList << " ";

  return tmpList;
}

void Installer::startConfigGen()
{

  if ( ! haveWarnedSpace )
     checkSpaceWarning();

  QStringList cfgList;

  // Generate the config file now
  cfgList+=getGlobalCfgSettings();

  cfgList+=getDiskCfgSettings();

  cfgList+=getComponentCfgSettings();

  // Save the install config script to disk
  cfgList << "runExtCommand=/root/save-config.sh";

  cfgList+= "";

  // If doing install from package disk
  if ( hasPkgsOnMedia )
    cfgList+=getDeskPkgCfg();

  cfgList+= "";

  if ( wheelCurItem != wFREEBSD && wheelCurItem != wPCSERVER && wheelCurItem != 12 ) {
    // Doing PC-BSD Install

    QString lang;
    if ( comboLanguage->currentIndex() != 0 )
      lang = languages.at(comboLanguage->currentIndex());
    else
      lang="en_US";

    // Setup the desktop
    cfgList << "runCommand=sh /usr/local/share/pcbsd/scripts/sys-init.sh desktop " + lang;

    // Setup for a fresh system first boot
    cfgList << "# Touch flags to enable PC-BSD setup at first boot";
    cfgList << "runCommand=touch /var/.runxsetup";
    cfgList << "runCommand=touch /var/.pcbsd-firstboot";
    cfgList << "runCommand=touch /var/.pcbsd-firstgui";

  } else if ( wheelCurItem == wPCSERVER || wheelCurItem == 12 ) {
    // Doing TrueOS Install
    cfgList+=getUsersCfgSettings();

    // Enable SSH?
    if ( fSSH )
      cfgList << "runCommand=echo 'sshd_enable=\"YES\"' >>/etc/rc.conf";

    // Setup the TrueOS server
    cfgList << "runCommand=sh /usr/local/share/pcbsd/scripts/sys-init.sh server";

  } else { // End of PC-BSD specific setup
    // Doing FreeBSD Install
    cfgList+=getUsersCfgSettings();

    // Enable SSH?
    if ( fSSH )
      cfgList << "runCommand=echo 'sshd_enable=\"YES\"' >>/etc/rc.conf";

  }

  // Run newaliases to fix mail errors
  cfgList << "runCommand=newaliases";

  // Now write out the cfgList to file
  QFile cfgfile( PCSYSINSTALLCFG );
  if ( cfgfile.open( QIODevice::WriteOnly ) ) {
    QTextStream stream( &cfgfile );
    for ( int i=0; i < cfgList.count(); ++i) {
      stream <<  cfgList.at(i) << "\n";
    }
    cfgfile.close();
  }
}

void Installer::slotHelp()
{
	pcHelp = new dialogHelp();
	switch (installStackWidget->currentIndex()) {
	case 0:
		pcHelp->dialogInit(HELPTEXT0);
		break;
	case 1:
		pcHelp->dialogInit(HELPTEXT1);
		break;
	case 2:
		pcHelp->dialogInit(HELPTEXT2);
		break;
	case 3:
		pcHelp->dialogInit(HELPTEXT3);
		break;
	default:
		pcHelp->dialogInit("No help text...");
		break;
	}
	pcHelp->show();
}

// Function which returns the pc-sysinstall cfg data
QStringList Installer::getDiskCfgSettings()
{
  QStringList tmpList;
  QList<QStringList> copyList;
  QString tmp, workingDisk, workingSlice, tmpSlice, XtraTmp, startPart, sliceSize;
  int disk = 0;

  // Copy over the list to a new variable we can mangle without modifying the original
  copyList = sysFinalDiskLayout;

  // Doing manual extraction
  if ( copyList.at(0).at(0) == "MANUAL" )
    return QStringList();

  while ( ! copyList.empty() )
  {
    workingDisk = copyList.at(0).at(0);
    workingSlice = copyList.at(0).at(1);
    tmpSlice = workingSlice;
    tmpList << "# Disk Setup for " + workingDisk ;

    // Check if this is an install to "Unused Space"
    for (int z=0; z < sysDisks.count(); ++z)
      if ( sysDisks.at(z).at(0) == "SLICE" \
        && sysDisks.at(z).at(2) == workingDisk + workingSlice \
        && sysDisks.at(z).at(4) == "Unused Space" )
          tmpSlice = "free";

    tmpList << "disk" + tmp.setNum(disk) + "=" + workingDisk;
    tmpList << "partition=" + tmpSlice;

    // Are we loading a boot-loader?
    if ( loadMBR )
      tmpList << "bootManager=bsd";
    else
      tmpList << "bootManager=none";

    // Set the GPT/MBR options
    if ( loadGPT ) 
      tmpList << "partscheme=GPT";
    else
      tmpList << "partscheme=MBR";

    tmpList << "commitDiskPart";
    tmpList << "";

    // If after doing the mirror, our list is empty, break out
    if ( copyList.empty() )
      break;
    
    // Now print the partition section for this slice
    tmpList << "# Partition Setup for " + workingDisk + "(" + workingSlice + ")";
    tmpList << "# All sizes are expressed in MB";
    tmpList << "# Avail FS Types, UFS, UFS+S, UFS+SUJ, UFS+J, ZFS, SWAP";
    tmpList << "# UFS.eli, UFS+S.eli, UFS+SUJ, UFS+J.eli, ZFS.eli, SWAP.eli";

    // If there is a dedicated /boot partition, need to list that first, see what is found
    for (int i=0; i < copyList.count(); ++i) {
      QStringList mounts = copyList.at(i).at(2).split(",");
      for (int z = 0; z < mounts.size(); ++z) {
        if ( copyList.at(i).at(0) == workingDisk \
          && copyList.at(i).at(1) == workingSlice \
          && mounts.at(z) == "/boot" )
		startPart="/boot";
      }
    }

    // If no dedicated /boot partition, then lets list "/" first
    if(startPart.isEmpty())
	startPart="/";

    // Start by looking for the root partition
    for (int i=0; i < copyList.count(); ++i) {
      QStringList mounts = copyList.at(i).at(2).split(",");
      for (int z = 0; z < mounts.size(); ++z) {
        if ( copyList.at(i).at(0) == workingDisk \
          && copyList.at(i).at(1) == workingSlice \
          && mounts.at(z) == startPart ) {

          // Check if we have any extra arguments to throw on the end
          XtraTmp="";
          if ( ! copyList.at(i).at(5).isEmpty() )
            XtraTmp=" (" + copyList.at(i).at(5) + ")" ;

          // Write out the partition line
          tmpList << "disk" + tmp.setNum(disk) + "-part=" \
                   + copyList.at(i).at(3) + " " + copyList.at(i).at(4) \
                   + " " + copyList.at(i).at(2) + XtraTmp;

          // Check if we have an encryption passphrase to use
          if ( ! copyList.at(i).at(6).isEmpty() )
	    tmpList << "encpass=" + copyList.at(i).at(6);

          // Done with this item, remove it now
          copyList.removeAt(i);
          break;
        }
      }
    }

    // Now look for SWAP
    for (int i=0; i < copyList.count(); ++i) {
      if ( copyList.at(i).at(0) == workingDisk \
        && copyList.at(i).at(1) == workingSlice \
        && copyList.at(i).at(2) == "SWAP" ) {

        // Write the partition line
        tmpList << "disk" + tmp.setNum(disk) + "-part=" \
                 + copyList.at(i).at(3) + " " + copyList.at(i).at(4) \
                 + " none";

        // Done with this item, remove it now
        copyList.removeAt(i);
        break;
      }
    }
 
    // Now look for any other partitions
    int count = copyList.count();
    for (int i=0; i < count; ++i) {
      if ( copyList.at(i).at(0) == workingDisk \
        && copyList.at(i).at(1) == workingSlice ) {

        // Check if we have any extra arguments to throw on the end
        XtraTmp="";
        if ( ! copyList.at(i).at(5).isEmpty() )
          XtraTmp=" (" + copyList.at(i).at(5) + ")" ;

	// If we are working on the last partition, set the size to 0 to use remaining disk
	if ( i == (count - 1) ) 
		sliceSize = "0";
	else
		sliceSize=copyList.at(i).at(4);

        // Write the partition line
        tmpList << "disk" + tmp.setNum(disk) + "-part=" \
                 + copyList.at(i).at(3) + " " + sliceSize \
                 + " " + copyList.at(i).at(2) + XtraTmp;

        // Check if we have an encryption passphrase to use
        if ( ! copyList.at(i).at(6).isEmpty() )
          tmpList << "encpass=" + copyList.at(i).at(6);

        // Done with this item, remove it now
        copyList.removeAt(i);
        i--;
        count--;
      }
    }


    // Close out this partition section
    tmpList << "commitDiskLabel";
    tmpList << "";

    // Increment our disk counter
    disk++;
  }

  return tmpList;
}

// Slot which checks any disk requirements before procceding to the next page
bool Installer::checkDiskRequirements()
{
  // For now just return true, the wizard should handle making sure
  // the user doesn't shoot themselves in the foot during disk setup
  return true;
}

// Function which begins the backend install, and connects slots to monitor it
void Installer::startInstall()
{
  // Disable the back / next buttons until we are finished
  nextButton->setEnabled(false);
  backButton->setEnabled(false);
  progressBarInstall->setValue(0); 
  installFoundCounter = false;
  installFoundMetaCounter = false;
  installFoundFetchOutput = false;

  // Setup some defaults for the secondary progress bar
  progressBarInstall2->setValue(0); 
  progressBarInstall2->setHidden(true); 
  labelInstallStatus2->setText("");
  labelInstallStatus2->setHidden(true);

  // Kill any hald instances, which causes failures to install when it
  // tries to mount our new partitions
  QProcess::execute("killall", QStringList() << "hald");

  // Start our process to begin the install
  QString program = PCSYSINSTALL;
  QStringList arguments;
  arguments << "-c" << PCSYSINSTALLCFG;

  installProc = new QProcess();
  installProc->setProcessChannelMode(QProcess::MergedChannels);
  connect(installProc,SIGNAL(finished( int, QProcess::ExitStatus)),this,SLOT(slotInstallProcFinished( int, QProcess::ExitStatus)));
  connect(installProc,SIGNAL(readyRead()),this,SLOT(slotReadInstallerOutput()));
  installProc->start(program, arguments);

}

// Function run when the install failed to prompt user for course of action
void Installer::installFailed()
{
   QString sysLog;
   labelInstallStatus->setText(tr("Failed!"));

   QMessageBox msgBox;
   msgBox.setWindowTitle(tr("PC-BSD Installer"));
   msgBox.setIcon(QMessageBox::Critical);
   msgBox.setText(tr("The installer has encountered an error and has been halted."));
   msgBox.setInformativeText(tr("Do you want to generate an error report?"));
   msgBox.setStandardButtons(QMessageBox::No | QMessageBox::Yes);
   msgBox.setDefaultButton(QMessageBox::Yes);

   // If we have a log, show it in the detailed view button
   if ( QFile::exists("/tmp/.pc-sysinstall/pc-sysinstall.log") )
   {
     QFile logFile("/tmp/.pc-sysinstall/pc-sysinstall.log");
     if (logFile.open(QIODevice::ReadOnly | QIODevice::Text))
         while (!logFile.atEnd())
           sysLog = sysLog + logFile.readLine() + "\n";
     msgBox.setDetailedText(sysLog);
   }
   int ret = msgBox.exec();

   switch (ret) {
   case QMessageBox::Yes:
       // Generate the error report
       Scripts::Backend::createErrorReport();
       break;
   case QMessageBox::No: // :)
       break;
   }

   QMessageBox msgBox2;
   msgBox2.setWindowTitle(tr("PC-BSD Installer"));
   msgBox2.setIcon(QMessageBox::Critical);
   msgBox2.setText(tr("Restart the system now?") );
   msgBox2.setStandardButtons(QMessageBox::No | QMessageBox::Yes);
   msgBox2.setDefaultButton(QMessageBox::Yes);
   msgBox2.setDetailedText(sysLog);

   ret = msgBox2.exec();

   switch (ret) {
   case QMessageBox::Yes:
       close();
       break;
   case QMessageBox::No: // :)
       break;
   }

}

// Slot which is called when the installation has finished
void Installer::slotInstallProcFinished( int exitCode, QProcess::ExitStatus status)
{
  QString tmp;
  if ( status != QProcess::NormalExit || exitCode != 0 )
  {
     installFailed();
  } else {
    // Move to the final page, and show a finish button
    proceed(true);
    nextButton->setEnabled(true);
    nextButton->setText(tr("&Finish"));
    nextButton->disconnect();
    connect(nextButton, SIGNAL(clicked()), this, SLOT(slotFinished()));
    backButton->setEnabled(false);
    abortButton->setEnabled(false);
  }
}

// Slot which reads the output of the installer
void Installer::slotReadInstallerOutput()
{
  QString tmp, line;
  int range;
  bool ok;


  while ( installProc->canReadLine() )
  {
     tmp = installProc->readLine();
     tmp.truncate(75);
     //qDebug() << tmp;

     // If doing a restore, don't bother checking for other values
     //if ( radioRestore->isChecked() ) {
     //   labelInstallStatus->setText(tmp);
     //	continue;
     //} 

     // Parse fetch output
     if ( installFoundFetchOutput ) {
       if ( tmp.indexOf("SIZE: ") != -1 ) {

          // Get the total range first
          line = tmp;
          tmp = tmp.remove(0, tmp.indexOf(":") + 2 );
          tmp.truncate(tmp.indexOf(" ")); 
          range = tmp.toInt(&ok);
          if ( ok )
             progressBarInstall->setRange(0, range + 1);

          // Now get the current progress
          tmp = line;
          tmp = tmp.remove(0, tmp.indexOf(":") + 2 );
          tmp = tmp.remove(0, tmp.indexOf(":") + 2 );
          range = tmp.toInt(&ok);
          if ( ok )
             progressBarInstall->setValue(range);
           
	  continue;
        } else {
          installFoundFetchOutput = false;
	  break;
        }
     } 

     // Unknown point in install
     if ( ! installFoundCounter && ! installFoundMetaCounter ) {

        // Check if we've found fetch output to update the progress bar with
        if ( tmp.indexOf("FETCH: ") != -1 ) {
          installFoundFetchOutput = true;
          break;
        }

        if ( tmp.indexOf("INSTALLCOUNT: ") != -1 ) {
          tmp = tmp.remove(0, tmp.indexOf(":") + 1 ); 
          range = tmp.toInt(&ok);
          if ( ok ) {
             range = range + 50;
             progressBarInstall->setRange(0, range + 1);  
             installFoundCounter = true;
	     if ( availDesktopPackageData )
                labelInstallStatus->setText(tr("Extracting system...")); 
	     else
                labelInstallStatus->setText(tr("Installing system... This may take a while...")); 
          }

	  break;

        } 

        // Check if we are on the meta-pkg installation
        if ( tmp.indexOf("Packages to install: ") != -1 ) {
          tmp = tmp.remove(0, tmp.indexOf(":") + 1 ); 
          range = tmp.toInt(&ok);
          if ( ok ) {
             progressBarInstall->setRange(0, range + 1);  
             progressBarInstall->setValue(0);  
             progressBarInstall2->setRange(0, 0);  
             labelInstallStatus2->setHidden(false);
             progressBarInstall2->setHidden(false);  
             installFoundMetaCounter = true;
             installFoundCounter = false;
             labelInstallStatus->setText(tr("Installing packages... This may take a while...")); 
	     continue;
          }

        } 

        labelInstallStatus->setText(tmp);
        continue; 
     } 

     // Doing file-extraction still
     if ( installFoundCounter ) {

       // Doing dist-files, may have multiple images to extract
       if ( tmp.indexOf("INSTALLCOUNT: ") != -1 ) {
         tmp = tmp.remove(0, tmp.indexOf(":") + 1 ); 
         range = tmp.toInt(&ok);
         if ( ok ) {
            progressBarInstall->setRange(0, range + 1);  
            installFoundCounter = true;
            if ( availDesktopPackageData )
              labelInstallStatus->setText(tr("Extracting system...")); 
            else
              labelInstallStatus->setText(tr("Installing system... This may take a while...")); 
         }
	 break;
       } 


       // Increment the progress
       progressBarInstall->setValue(progressBarInstall->value() + 1); 

       // We've reached the end of this counted section
       if ( tmp.indexOf("Extraction Finished") != -1 ) {
         installFoundCounter = false;
         progressBarInstall->setRange(0, 0);  
       }

       continue;
     }

     // Doing meta-pkgs
     if ( installFoundMetaCounter ) {
        if ( tmp.indexOf("Package installation complete!") != -1 ) {
           installFoundMetaCounter = false;
           progressBarInstall->setRange(0, 0);  
           progressBarInstall2->setHidden(true);
           labelInstallStatus2->setHidden(true);
	   continue;
        }

	// Got this far, increment the pkg-sub count
        progressBarInstall->setValue(progressBarInstall->value() + 1); 
        labelInstallStatus2->setText(tmp);
     }

  } // end of while loop
}

// Return list of components to install
QStringList Installer::getComponentCfgSettings()
{
  QStringList componentList, com;
  if ( fSRC )
    com << "src";
  if ( fPORTS )
    com << "ports";

  if ( ! com.isEmpty() ) {
    componentList << "";
    componentList << "# Optional Components";
    componentList << "installComponents=" + com.join(",");
  }

  return componentList;
}

// Start xvkbd
void Installer::slotPushVirtKeyboard()
{
   system("killall -9 xvkbd; xvkbd -compact &");
}

// Return the configuration for desktop packages
QStringList Installer::getDeskPkgCfg()
{
   if ( wheelCurItem == wFREEBSD )
      return QStringList();

   QStringList cfgList, pkgList;
   QString line;

   QList<QStringList> curList;

   if ( wheelCurItem != wPCSERVER && wheelCurItem != 11 && wheelCurItem != 12) {
     curList = listDeskPkgs;
     pkgList << "pcbsd-base";
   } else {
     curList = listServerPkgs;
     pkgList << "trueos-base";
   }

   // Loop though list of pkgs, see what to install
   for ( int d=0; d < curList.count(); ++d) {
     for ( int i=0; i < selectedPkgs.count(); ++i)
        // Is the package selected or the base-system?
	if ( curList.at(d).at(0) == selectedPkgs.at(i) || curList.at(d).at(0) == "base-system" ) {

           // Yay! Lets get a list of packages to install
	   QFile mFile;
           mFile.setFileName(curList.at(d).at(6));
           if ( ! mFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
	      qDebug() << "Invalid meta-pkg list:" << curList.at(d).at(6);
	      break;
	   }
  
           // Read in the meta pkg list
           QTextStream in(&mFile);
           while ( !in.atEnd() ) {
             line = in.readLine().simplified();
	     if ( line.isEmpty() )
                 continue; 
	     
             pkgList << line.section(":", 0,0);
           }
           mFile.close();
	   break;
	}
   }

   cfgList << "installPackages=" + pkgList.join(" ");
   return cfgList;
}

// Return list of users in cfg format
QStringList Installer::getUsersCfgSettings()
{
   QStringList userList;
  
   userList << "";
   userList << "# Root Password";
   userList << "rootPass=" + fRootPW;

   userList << "";
   userList << "# Users";
   userList << "userName=" + fUserName;
   userList << "userComment=" + fName;
   userList << "userPass=" + fUserPW;
   userList << "userShell=" + fShell;
   userList << "userHome=/home/" + fUserName;
   userList << "userGroups=wheel,operator";
   userList << "commitUser";
   userList << "";
 
   return userList;
}

void Installer::closeEvent(QCloseEvent *event)
{
    int ret = QMessageBox::question(this, tr("PC-BSD Installer"),
                                tr("Are you sure you want to abort this installation?"),
                                QMessageBox::No | QMessageBox::Yes,
                                QMessageBox::No);
    switch (ret) {
    case QMessageBox::Yes:
        //exit the installer :(
        break;
    case QMessageBox::No: // :)
        event->ignore();
        break;
    }
}

void Installer::checkSpaceWarning()
{
  int totalSize = -1;
  int targetSize;
  int targetLoc;
  bool ok;
  QString workingDisk = sysFinalDiskLayout.at(0).at(0);
  QString workingSlice = sysFinalDiskLayout.at(0).at(1);
  QString targetType;
  QString target;
  //qDebug() << "Disk layout:" << workingDisk << workingSlice;

  if ( workingSlice == "ALL" ) {
    targetType = "DRIVE";
    target = workingDisk;
    targetLoc = 1;
  } else {
    targetType = "SLICE";
    target = workingDisk + workingSlice;
    targetLoc = 2;
  }
  
  // Lets get the size for this disk / partition
  for (int i=0; i < sysDisks.count(); ++i) {
      // Make sure to only add the slices to the listDiskSlices
      if ( sysDisks.at(i).at(0) == targetType && target == sysDisks.at(i).at(targetLoc))
        totalSize = sysDisks.at(i).at(targetLoc + 1).toInt(&ok);
  }

  //qDebug() << totalSize;

  if ( installStackWidget->currentIndex() == 1 && (wheelCurItem == wFREEBSD || wheelCurItem == wPCSERVER || wheelCurItem == 12) )
     targetSize=20000;
  else
     targetSize=50000;

  qDebug() << totalSize << targetSize;

  // Lets print a nice handy warning for users with possible
  // low disk space issues
  if ( totalSize < targetSize ) {
  QMessageBox::warning(this, tr("PC-BSD Installer"),
      QString(tr("The selected disk / partition is less than recommended %1MB. The installation may fail...")).arg(targetSize),
      QMessageBox::Ok,
      QMessageBox::Ok);
      haveWarnedSpace = true;      
  }

  return;
}
