# login -- a normal login
alias login='exec login -p -R-p -R-aHOME -R-aMOTD -R-e_LOGIN_RETRY=yes'
alias logon=login
alias l=login
# quick login -- don't act like a login shell, but do cd to $HOME
alias ql='exec login -pSL -aMOTD -R-p -R-aHOME -R-aMOTD -R-e_LOGIN_RETRY=yes'
# su -- don't even cd to $HOME
alias su='exec login --program-name=su -pSL -aHOME -aMOTD -R-p -R-aHOME -R-aMOTD -R-e_LOGIN_RETRY=yes'
alias sush=su
alias help='cat $HOME/README'
alias '?'=help
