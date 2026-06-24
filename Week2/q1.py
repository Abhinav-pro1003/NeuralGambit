import json
import copy  # use it for deepcopy if needed
import math  # for math.inf
import logging

logging.basicConfig(format='%(levelname)s - %(asctime)s - %(message)s', datefmt='%d-%b-%y %H:%M:%S',
                    level=logging.INFO)

# Global variables in which you need to store player strategies (this is data structure that'll be used for evaluation)
# Mapping from histories (str) to probability distribution over actions
strategy_dict_x = {}
strategy_dict_o = {}


class History:
    def __init__(self, history=None):
        """
        # self.history : Eg: [0, 4, 2, 5]
            keeps track of sequence of actions played since the beginning of the game.
            Each action is an integer between 0-8 representing the square in which the move will be played as shown
            below.
              ___ ___ ____
             |_0_|_1_|_2_|
             |_3_|_4_|_5_|
             |_6_|_7_|_8_|

        # self.board
            empty squares are represented using '0' and occupied squares are either 'x' or 'o'.
            Eg: ['x', '0', 'x', '0', 'o', 'o', '0', '0', '0']
            for board
              ___ ___ ____
             |_x_|___|_x_|
             |___|_o_|_o_|
             |___|___|___|

        # self.player: 'x' or 'o'
            Player whose turn it is at the current history/board

        :param history: list keeps track of sequence of actions played since the beginning of the game.
        """
        if history is not None:
            self.history = history
            self.board = self.get_board()
        else:
            self.history = []
            self.board = ['0', '0', '0', '0', '0', '0', '0', '0', '0']
        self.player = self.current_player()

    def current_player(self):
        """ Player function
        Get player whose turn it is at the current history/board
        :return: 'x' or 'o' or None
        """
        total_num_moves = len(self.history)
        if total_num_moves < 9:
            if total_num_moves % 2 == 0:
                return 'x'
            else:
                return 'o'
        else:
            return None

    def get_board(self):
        """ Play out the current self.history and get the board corresponding to the history in self.board.

        :return: list Eg: ['x', '0', 'x', '0', 'o', 'o', '0', '0', '0']
        """
        board = ['0', '0', '0', '0', '0', '0', '0', '0', '0']
        for i in range(len(self.history)):
            if i % 2 == 0:
                board[self.history[i]] = 'x'
            else:
                board[self.history[i]] = 'o'
        return board

    def is_win(self):
        # check if the board position is a win for either players
        # Feel free to implement this in anyway if needed

        for i in range(3):
            cntx = 0
            cnto = 0
            for j in range(3):
                if self.board[3*i+j] == 'x':
                    cntx += 1
                if self.board[3*i+j] == 'o':
                    cnto += 1
            if cntx ==3:
                return 'x'
            elif cnto == 3:
                return 'o'
        
        for j in range(3):
            cntx = 0
            cnto = 0
            for i in range(3):
                if self.board[3*i+j] == 'x':
                    cntx += 1
                if self.board[3*i+j] == 'o':
                    cnto += 1
            if cntx ==3:
                return 'x'
            elif cnto == 3:
                return 'o'
        
        cntx = 0
        cnto = 0
        for i in range(3):
            if self.board[4*i] == 'x':
                    cntx += 1
            if self.board[4*i] == 'o':
                cnto += 1
        if cntx ==3:
            return 'x'
        elif cnto == 3:
            return 'o'

        cntx = 0
        cnto = 0
        for i in range(3):
            if self.board[2*i+2] == 'x':
                    cntx += 1
            if self.board[2*i+2] == 'o':
                cnto += 1
        if cntx ==3:
            return 'x'
        elif cnto == 3:
            return 'o'
        return '0'

    def is_draw(self):
        # check if the board position is a draw
        # Feel free to implement this in anyway if needed
        return self.is_win() == '0' and '0' not in self.board
        

    def get_valid_actions(self):
        # get the empty squares from the board
        # Feel free to implement this in anyway if needed
        actions = []
        for i in range(3):
            for j in range(3):
                if self.board[3*i+j] == '0':
                    actions.append(3*i+j)
        return actions

    def is_terminal_history(self):
        # check if the history is a terminal history
        # Feel free to implement this in anyway if needed
        if self.is_win()!='0' or self.is_draw():
            return True
        return False

    def get_utility_given_terminal_history(self):
        # Feel free to implement this in anyway if needed
        winner = self.is_win()

        if winner == 'x':
            return 1
        if winner == 'o':
            return -1
        return 0    
        

    def update_history(self, action):
        # In case you need to create a deepcopy and update the history obj to get the next history object.
        # Feel free to implement this in anyway if needed
        self.history.append(action)
        self.board = self.get_board()
        self.player = self.current_player()
        return
        


def backward_induction(history_obj):
    """
    :param history_obj: Histroy class object
    :return: best achievable utility (float) for th current history_obj
    """
    global strategy_dict_x, strategy_dict_o
    # TODO implement
    # (1) Implement backward induction for tictactoe
    # (2) Update the global variables strategy_dict_x or strategy_dict_o which are a mapping from histories to
    # probability distribution over actions.
    # (2a)These are dictionary with keys as string representation of the history list e.g. if the history list of the
    # history_obj is [0, 4, 2, 5], then the key is "0425". Each value is in turn a dictionary with keys as actions 0-8
    # (str "0", "1", ..., "8") and each value of this dictionary is a float (representing the probability of
    # choosing that action). Example: {”0452”: {”0”: 0, ”1”: 0, ”2”: 0, ”3”: 0, ”4”: 0, ”5”: 0, ”6”: 1, ”7”: 0, ”8”:
    # 0}}
    # (2b) Note, the strategy for each history in strategy_dict_x and strategy_dict_o is probability distribution over
    # actions. But since tictactoe is a PIEFG, there always exists an optimal deterministic strategy (SPNE). So your
    # policy will be something like this {"0": 1, "1": 0, "2": 0, "3": 0, "4": 0, "5": 0, "6": 0, "7": 0, "8": 0} where
    # "0" was the one of the best actions for the current player/history.
    # TODO implement

    his = ""

    for h in history_obj.history:
        his += str(h)

    if history_obj.is_terminal_history():
        u = history_obj.get_utility_given_terminal_history()
        return u

    actions = history_obj.get_valid_actions()
    if history_obj.current_player() == 'x':
        best_u = -math.inf
    else:
        best_u = math.inf
    best_action = -1
    priority = [4, 0, 2, 6, 8, 1, 3, 5, 7, -1]

    for a in actions:
        new_hist = copy.deepcopy(history_obj)
        new_hist.update_history(a)
        u = backward_induction(new_hist)
        if history_obj.current_player() == 'x':
            if u > best_u:
                best_u = u
                best_action = a
            elif u == best_u:
                if priority.index(a) < priority.index(best_action):
                    best_action = a
        else:
            if u < best_u:
                best_u = u
                best_action = a
            elif u == best_u:
                if priority.index(a) < priority.index(best_action):
                    best_action = a
    
    if history_obj.current_player() == 'x':
        strategy_dict_x[his] = {"0": 0, "1": 0, "2": 0, "3": 0, "4": 0, "5": 0, "6": 0, "7": 0, "8": 0}
        strategy_dict_x[his][str(best_action)] = 1
    else:
        strategy_dict_o[his] = {"0": 0, "1": 0, "2": 0, "3": 0, "4": 0, "5": 0, "6": 0, "7": 0, "8": 0}
        strategy_dict_o[his][str(best_action)] = 1
    
    return best_u


def solve_tictactoe():
    backward_induction(History())
    with open('./policy_x.json', 'w') as f:
        json.dump(strategy_dict_x, f)
    with open('./policy_o.json', 'w') as f:
        json.dump(strategy_dict_o, f)
    return strategy_dict_x, strategy_dict_o


if __name__ == "__main__":
    logging.info("Start")
    solve_tictactoe()
    logging.info("End")
