#include <QFileDialog>
#include <QDebug>
#include <QAction>
#include <QMenuBar>
#include <QMenu>
#include <QToolBox>
#include <QListView>
#include <QTabWidget>
#include <QUndoStack>
#include <QSettings>
#include <QCoreApplication>
#include <QFileInfo>
#include <QFontDatabase>
#include <QSplitter>
#include <QScrollArea>
#include <QStackedWidget>

#include "powertabeditor.h"
#include "scorearea.h"
#include "painters/caret.h"
#include "skinmanager.h"
#include "dialogs/preferencesdialog.h"
#include "powertabdocument/powertabdocument.h"
#include "widgets/mixer/mixer.h"
#include "widgets/toolbox/toolbox.h"
#include "midiplayer.h"
#include "actions/undomanager.h"

#include <actions/removechordtext.h>
#include <actions/addchordtext.h>

#include <dialogs/chordnamedialog.h>

QTabWidget* PowerTabEditor::tabWidget = NULL;
UndoManager* PowerTabEditor::undoManager = NULL;
QSplitter* PowerTabEditor::vertSplitter = NULL;
QSplitter* PowerTabEditor::horSplitter = NULL;
Toolbox* PowerTabEditor::toolBox = NULL;

PowerTabEditor::PowerTabEditor(QWidget *parent) :
        QMainWindow(parent)
{
    this->setWindowIcon(QIcon(":icons/app_icon.png"));

    // load fonts from the resource file
    QFontDatabase::addApplicationFont(":fonts/emmentaler-13.otf"); // used for music notation
    QFontDatabase::addApplicationFont(":fonts/LiberationSans-Regular.ttf"); // used for tab notes

    // load application settings
    QCoreApplication::setOrganizationName("Power Tab");
    QCoreApplication::setApplicationName("Power Tab Editor");
    QSettings settings;
    // retrieve the previous directory that a file was opened/saved to (default value is home directory)
    previousDirectory = settings.value("app/previousDirectory", QDir::homePath()).toString();

    undoManager = new UndoManager();
    connect(undoManager, SIGNAL(indexChanged(int)), this, SLOT(RefreshOnUndoRedo(int)));

    skinManager = new SkinManager("default");

    midiPlayer = NULL;

    CreateActions();
    CreateMenus();
    CreateTabArea();

    isPlaying = false;

    preferencesDialog = new PreferencesDialog();

    setMinimumSize(800, 600);
    setWindowState(Qt::WindowMaximized);
    setWindowTitle(tr("Power Tab Editor 2.0"));

    horSplitter = new QSplitter();
    horSplitter->setOrientation(Qt::Horizontal);

    toolBox = new Toolbox(0,skinManager);
    horSplitter->addWidget(toolBox);
    horSplitter->addWidget(tabWidget);

    vertSplitter = new QSplitter();
    vertSplitter->setOrientation(Qt::Vertical);

    vertSplitter->addWidget(horSplitter);

    mixerList = new QStackedWidget;
    mixerList->setMinimumHeight(150);
    vertSplitter->addWidget(mixerList);

    setCentralWidget(vertSplitter);
}

PowerTabEditor::~PowerTabEditor()
{
    delete preferencesDialog;
    delete skinManager;
    delete midiPlayer;
    delete undoManager;
}

// Redraws the *entire* document upon undo/redo
// TODO - notify the appropriate painter to redraw itself, instead
// of redrawing the whole score
void PowerTabEditor::RefreshOnUndoRedo(int index)
{
    Q_UNUSED(index);
    RefreshCurrentDocument();
    connect(getCurrentScoreArea()->getCaret(), SIGNAL(moved()), this, SLOT(updateActions()));
    updateActions();
}

void PowerTabEditor::CreateActions()
{
    // File-related actions
    openFileAct = new QAction(tr("&Open..."), this);
    openFileAct->setShortcuts(QKeySequence::Open);
    openFileAct->setStatusTip(tr("Open an existing document"));
    connect(openFileAct, SIGNAL(triggered()), this, SLOT(OpenFile()));

    preferencesAct = new QAction(tr("&Preferences..."), this);
    preferencesAct->setShortcuts(QKeySequence::Preferences);
    connect(preferencesAct, SIGNAL(triggered()), this, SLOT(OpenPreferences()));

    // Exit the application
    exitAppAct = new QAction(tr("&Quit"), this);
    exitAppAct->setShortcuts(QKeySequence::Quit);
    exitAppAct->setStatusTip(tr("Exit the application"));
    connect(exitAppAct, SIGNAL(triggered()), this, SLOT(close()));

    // Redo / Undo actions
    undoAct = undoManager->createUndoAction(this, tr("&Undo"));
    undoAct->setShortcuts(QKeySequence::Undo);

    redoAct = undoManager->createRedoAction(this, tr("&Redo"));
    redoAct->setShortcuts(QKeySequence::Redo);

    // Playback-related actions
    playPauseAct = new QAction(tr("Play"), this);
    playPauseAct->setShortcut(QKeySequence(Qt::Key_Space));
    connect(playPauseAct, SIGNAL(triggered()), this, SLOT(startStopPlayback()));

    // Section navigation actions
    firstSectionAct = new QAction(tr("First Section"), this);
    firstSectionAct->setShortcuts(QKeySequence::MoveToStartOfDocument);
    connect(firstSectionAct, SIGNAL(triggered()), this, SLOT(moveCaretToFirstSection()));

    nextSectionAct = new QAction(tr("Next Section"), this);
    nextSectionAct->setShortcuts(QKeySequence::MoveToNextPage);
    connect(nextSectionAct, SIGNAL(triggered()), this, SLOT(moveCaretToNextSection()));

    prevSectionAct = new QAction(tr("Previous Section"), this);
    prevSectionAct->setShortcuts(QKeySequence::MoveToPreviousPage);
    connect(prevSectionAct, SIGNAL(triggered()), this, SLOT(moveCaretToPrevSection()));

    lastSectionAct = new QAction(tr("Last Section"), this);
    lastSectionAct->setShortcuts(QKeySequence::MoveToEndOfDocument);
    connect(lastSectionAct, SIGNAL(triggered()), this, SLOT(moveCaretToLastSection()));

    // Position-related actions
    startPositionAct = new QAction(tr("Move to &Start"), this);
    startPositionAct->setShortcuts(QKeySequence::MoveToStartOfLine);
    connect(startPositionAct, SIGNAL(triggered()), this, SLOT(moveCaretToStart()));

    nextPositionAct = new QAction(tr("&Next Position"), this);
    nextPositionAct->setShortcuts(QKeySequence::MoveToNextChar);
    connect(nextPositionAct, SIGNAL(triggered()), this, SLOT(moveCaretRight()));

    prevPositionAct = new QAction(tr("&Previous Position"), this);
    prevPositionAct->setShortcuts(QKeySequence::MoveToPreviousChar);
    connect(prevPositionAct, SIGNAL(triggered()), this, SLOT(moveCaretLeft()));

    nextStringAct = new QAction(tr("Next String"), this);
    nextStringAct->setShortcuts(QKeySequence::MoveToNextLine);
    connect(nextStringAct, SIGNAL(triggered()), this, SLOT(moveCaretDown()));

    prevStringAct = new QAction(tr("Previous String"), this);
    prevStringAct->setShortcuts(QKeySequence::MoveToPreviousLine);
    connect(prevStringAct, SIGNAL(triggered()), this, SLOT(moveCaretUp()));

    lastPositionAct = new QAction(tr("Move to &End"), this);
    lastPositionAct->setShortcuts(QKeySequence::MoveToEndOfLine);
    connect(lastPositionAct, SIGNAL(triggered()), this, SLOT(moveCaretToEnd()));

    nextStaffAct = new QAction(tr("Next Staff"), this);
    nextStaffAct->setShortcut(QKeySequence(Qt::ALT + Qt::Key_Down));
    connect(nextStaffAct, SIGNAL(triggered()), this, SLOT(moveCaretToNextStaff()));

    prevStaffAct = new QAction(tr("Previous Staff"), this);
    prevStaffAct->setShortcut(QKeySequence(Qt::ALT + Qt::Key_Up));
    connect(prevStaffAct, SIGNAL(triggered()), this, SLOT(moveCaretToPrevStaff()));

    // Text-related actions
    chordNameAct = new QAction(tr("Chord Name..."), this);
    chordNameAct->setShortcut(QKeySequence(Qt::Key_C));
    chordNameAct->setCheckable(true);
    connect(chordNameAct, SIGNAL(triggered()), this, SLOT(editChordName()));
}

void PowerTabEditor::CreateMenus()
{
    // File Menu
    fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(openFileAct);
    fileMenu->addSeparator();
    fileMenu->addAction(preferencesAct);
    fileMenu->addSeparator();
    fileMenu->addAction(exitAppAct);

    // Edit Menu
    editMenu = menuBar()->addMenu(tr("&Edit"));
    editMenu->addAction(undoAct);
    editMenu->addAction(redoAct);

    // Playback Menu
    playbackMenu = menuBar()->addMenu(tr("Play&back"));
    playbackMenu->addAction(playPauseAct);

    // Position Menu
    positionMenu = menuBar()->addMenu(tr("&Position"));

    positionSectionMenu = positionMenu->addMenu(tr("&Section"));
    positionSectionMenu->addAction(firstSectionAct);
    positionSectionMenu->addAction(nextSectionAct);
    positionSectionMenu->addAction(prevSectionAct);
    positionSectionMenu->addAction(lastSectionAct);

    positionStaffMenu = positionMenu->addMenu(tr("&Staff"));
    positionStaffMenu->addAction(startPositionAct);
    positionStaffMenu->addAction(nextPositionAct);
    positionStaffMenu->addAction(prevPositionAct);
    positionStaffMenu->addAction(nextStringAct);
    positionStaffMenu->addAction(prevStringAct);
    positionStaffMenu->addAction(lastPositionAct);
    positionStaffMenu->addAction(nextStaffAct);
    positionStaffMenu->addAction(prevStaffAct);

    // Text Menu
    textMenu = menuBar()->addMenu(tr("&Text"));
    textMenu->addAction(chordNameAct);
}

void PowerTabEditor::CreateTabArea()
{
    tabWidget = new QTabWidget;
    tabWidget->setTabsClosable(true);

    tabWidget->setStyleSheet(skinManager->getDocumentTabStyle());

    // creates a new document by default
    /*ScoreArea* score = new ScoreArea;
    score->RenderDocument(documentManager.getCurrentDocument());
    tabWidget->addTab(score, tr("Untitled"));*/

    connect(tabWidget, SIGNAL(tabCloseRequested(int)), this, SLOT(closeTab(int)));
    connect(tabWidget, SIGNAL(currentChanged(int)), this, SLOT(switchTab(int)));
}

// Open a new file
void PowerTabEditor::OpenFile()
{
    QString fileFilter;
    QString fileName = QFileDialog::getOpenFileName(this, tr("Open"), previousDirectory, fileFilter);

    if (fileName.isEmpty())
    {
        qDebug() << "No file selected";
    }
    else
    {
        qDebug() << "Opening file: " << fileName;
        bool success = documentManager.Add(fileName); // add the file to the document manager

        // draw the score if it was successful
        if (success)
        {
            ScoreArea* score = new ScoreArea;
            score->RenderDocument(documentManager.getCurrentDocument());
            QFileInfo fileInfo(fileName);
            // save this as the previous directory
            previousDirectory = fileInfo.absolutePath();
            QSettings settings;
            settings.setValue("app/previousDirectory", previousDirectory);

            QString title = fileInfo.fileName();
            QFontMetrics fm (tabWidget->font());

            bool chopped = false;

            // each tab is 200px wide, so we want to shorten the name if it's wider than 140px
            while(fm.width(title)>140)
            {
                title.chop(1);
                chopped = true;
            }

            if (chopped)
                title.append("...");

            undoManager->addNewUndoStack();

            connect(score->getCaret(), SIGNAL(moved()), this, SLOT(updateActions()));

            tabWidget->addTab(score, title);

            // add the guitars to a new mixer
            Mixer* mixer = new Mixer(0,skinManager);
            QScrollArea* scrollArea = new QScrollArea;
            PowerTabDocument* doc = documentManager.getCurrentDocument();
            for (quint32 i=0; i < doc->GetGuitarScore()->GetGuitarCount(); i++)
            {
                mixer->AddInstrument(doc->GetGuitarScore()->GetGuitar(i));
            }
            scrollArea->setWidget(mixer);
            mixerList->addWidget(scrollArea);

            // switch to the new document
            tabWidget->setCurrentIndex(documentManager.getCurrentDocumentIndex());

            updateActions();
        }
    }
}

// Opens the preferences dialog
void PowerTabEditor::OpenPreferences()
{
    preferencesDialog->exec();
}

// Redraws the whole score of the current document
void PowerTabEditor::RefreshCurrentDocument()
{
    ScoreArea* currentDoc = reinterpret_cast<ScoreArea *>(tabWidget->currentWidget());
    currentDoc->RenderDocument();
}

// Close a document and clean up
void PowerTabEditor::closeTab(int index)
{
    undoManager->removeStack(index);
    documentManager.Remove(index);
    delete tabWidget->widget(index);
    tabWidget->removeTab(index);

    mixerList->removeWidget(mixerList->widget(index));

    // get the index of the tab that we will now switch to
    const int currentIndex = tabWidget->currentIndex();

    undoManager->setActiveStackIndex(currentIndex);
    mixerList->setCurrentIndex(currentIndex);
    documentManager.setCurrentDocumentIndex(currentIndex);
}

// When the tab is switched, switch the current document in the document manager
void PowerTabEditor::switchTab(int index)
{
    documentManager.setCurrentDocumentIndex(index);
    mixerList->setCurrentIndex(index);
    undoManager->setActiveStackIndex(index);

    if(documentManager.getCurrentDocument())
    {
        QString title(documentManager.getCurrentDocument()->GetFileName().c_str());
        title.remove(0,title.lastIndexOf("/")+1);
        setWindowTitle(title+tr(" - Power Tab Editor 2.0"));
    }
    else
    {
        setWindowTitle(tr("Power Tab Editor 2.0"));
    }
}

ScoreArea* PowerTabEditor::getCurrentScoreArea()
{
    return reinterpret_cast<ScoreArea*>(tabWidget->currentWidget());
}

void PowerTabEditor::startStopPlayback()
{
    isPlaying = !isPlaying;

    if (isPlaying)
    {
        playPauseAct->setText(tr("Pause"));

        getCurrentScoreArea()->getCaret()->setPlaybackMode(true);

        moveCaretToStart();

        midiPlayer = new MidiPlayer(getCurrentScoreArea()->getCaret());
        midiPlayer->play();
    }
    else
    {
        playPauseAct->setText(tr("Play"));

        getCurrentScoreArea()->getCaret()->setPlaybackMode(false);
        midiPlayer->stop();
        delete midiPlayer;
        midiPlayer = NULL;
    }
}

bool PowerTabEditor::moveCaretRight()
{
    return getCurrentScoreArea()->getCaret()->moveCaretHorizontal(1);
}

bool PowerTabEditor::moveCaretLeft()
{
    return getCurrentScoreArea()->getCaret()->moveCaretHorizontal(-1);
}

void PowerTabEditor::moveCaretDown()
{
    getCurrentScoreArea()->getCaret()->moveCaretVertical(1);
}

void PowerTabEditor::moveCaretUp()
{
    getCurrentScoreArea()->getCaret()->moveCaretVertical(-1);
}

void PowerTabEditor::moveCaretToStart()
{
    getCurrentScoreArea()->getCaret()->moveCaretToStart();
}

void PowerTabEditor::moveCaretToEnd()
{
    getCurrentScoreArea()->getCaret()->moveCaretToEnd();
}

void PowerTabEditor::moveCaretToFirstSection()
{
    getCurrentScoreArea()->getCaret()->moveCaretToFirstSection();
}

bool PowerTabEditor::moveCaretToNextSection()
{
    return getCurrentScoreArea()->getCaret()->moveCaretSection(1);;
}

bool PowerTabEditor::moveCaretToPrevSection()
{
    return getCurrentScoreArea()->getCaret()->moveCaretSection(-1);
}

void PowerTabEditor::moveCaretToLastSection()
{
    getCurrentScoreArea()->getCaret()->moveCaretToLastSection();
}

bool PowerTabEditor::moveCaretToNextStaff()
{
    return getCurrentScoreArea()->getCaret()->moveCaretStaff(1);;
}

bool PowerTabEditor::moveCaretToPrevStaff()
{
    return getCurrentScoreArea()->getCaret()->moveCaretStaff(-1);
}

// If there is a chord name at the current position, remove it
// If there is no chord name, show the dialog to add a chord name
// Existing chord names are edited by clicking on the chord name
void PowerTabEditor::editChordName()
{
    // Find if there is a chord name at the current position
    Caret* caret = getCurrentScoreArea()->getCaret();
    const quint32 caretPosition = caret->getCurrentPositionIndex();
    System* currentSystem = caret->getCurrentSystem();

    int chordTextIndex = caret->getCurrentSystem()->FindChordText(caretPosition);
    if (chordTextIndex == -1) // if not found, add a new chord name
    {
        chordTextIndex = 0;
        ChordName chordName;
        ChordNameDialog chordNameDialog(&chordName);
        if (chordNameDialog.exec() == QDialog::Accepted)
        {
            ChordText* chordText = new ChordText(caretPosition, chordName);
            undoManager->push(new AddChordText(currentSystem, chordText, chordTextIndex));
        }
    }
    else // if found, remove the chord name
    {
        undoManager->push(new RemoveChordText(currentSystem, chordTextIndex));
    }

}

// Updates whether menu items are checked, etc, whenever the caret moves
void PowerTabEditor::updateActions()
{
    Caret* caret = getCurrentScoreArea()->getCaret();
    const quint32 caretPosition = caret->getCurrentPositionIndex();
    System* currentSystem = caret->getCurrentSystem();

    // Check for chord text
    if (currentSystem->HasChordText(caretPosition))
    {
        chordNameAct->setChecked(true);
    }
    else
    {
        chordNameAct->setChecked(false);
    }
}
