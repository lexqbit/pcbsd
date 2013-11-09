#include "updatecontroller.h"

CAbstractUpdateController::CAbstractUpdateController()
{
    mCurrentState= eNOT_INITIALIZED;
}

CAbstractUpdateController::EUpdateControllerState CAbstractUpdateController::curentState()
{
    return mCurrentState;
}

void CAbstractUpdateController::setCurrentState(CAbstractUpdateController::EUpdateControllerState new_state)
{
    mCurrentState= new_state;
    emit stateChanged(mCurrentState);
}

void CAbstractUpdateController::reportProgress(CAbstractUpdateController::SProgress curr_progress)
{
    mCurrentProgress= curr_progress;
    emit progress(mCurrentProgress);
}

void CAbstractUpdateController::check()
{
    //TODO: correct current state check
    setCurrentState(eCHECKING);
    onCheckUpdates();
}

void CAbstractUpdateController::updateAll()
{
    //TODO: correct current state check
    setCurrentState(eUPDATING);
    onUpdateAll();
}
