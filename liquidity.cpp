const unsigned long multiplier=10000;
const unsigned int sig_figs=5;

#define SYMBOL_MAX 7

extern "C" void read_settings(const char * const, char * const, char * const, char * const, char * const, char * const, char * const);
char sender_comp_id[64], target_comp_id[64], target_sub_id[64], username[64], password[64], account[64];

#include "die.h"

#include <limits.h>
#include <math.h>
#include <ncurses.h>
#include <quickfix/Field.h>
#include <quickfix/fix44/NewOrderSingle.h>
#include <stdlib.h>
#include <quickfix/SessionSettings.h>
#include <quickfix/FileStore.h>
#include <quickfix/FileLog.h>
#include <quickfix/SocketInitiator.h>
#include <quickfix/fix44/MarketDataRequest.h>
#include <stdio.h>
#include <sstream>
#include <time.h>
#include <quickfix/FixValues.h>
#include <quickfix/Application.h>
#include <quickfix/MessageCracker.h>
#include <quickfix/Message.h>
#include <quickfix/fix44/CollateralInquiry.h>
#include <quickfix/fix44/CollateralInquiryAck.h>
#include <quickfix/fix44/CollateralReport.h>
#include <quickfix/fix44/UserRequest.h>
#include <quickfix/fix44/MarketDataSnapshotFullRefresh.h>
#include <quickfix/fix44/OrderMassStatusRequest.h>
#include <quickfix/fix44/OrderCancelRequest.h>
#include <quickfix/fix44/ExecutionReport.h>
#include <quickfix/fix44/RequestForPositions.h>
#include <quickfix/fix44/RequestForPositionsAck.h>
#include <quickfix/fix44/PositionReport.h>
#include <map>

long lmin(const long a, const long b){
	if(a>b) return b; else return a; }

unsigned int next_id=0;

std::string serial_id(){
	std::stringstream s;
	s<<next_id++;
	return s.str(); }

void set_header_fields(FIX::Message& message){
	message.getHeader().setField(FIX::SenderCompID(sender_comp_id));
	message.getHeader().setField(FIX::TargetCompID(target_comp_id));
	message.getHeader().setField(FIX::TargetSubID(target_sub_id)); }

void send_order(FIX::Symbol symbol, double v, double p, const std::string& cl_ord_id){
	FIX44::NewOrderSingle order(
		FIX::ClOrdID(cl_ord_id),
		(v>0)?
			FIX::Side(FIX::Side_BUY):
			FIX::Side(FIX::Side_SELL),
		FIX::TransactTime(),
		FIX::OrdType(FIX::OrdType_LIMIT));
	set_header_fields(order);
	if(v>0)
		order.set(FIX::OrderQty(multiplier*v));
		else order.set(FIX::OrderQty(multiplier*-v));
	order.set(FIX::HandlInst('1'));
	order.set(symbol);
	order.set(FIX::TimeInForce(FIX::TimeInForce_GOOD_TILL_CANCEL));
	order.set(FIX::Price(p));
	order.set(FIX::Account(account));
	FIX::Session::sendToTarget(order); }

void cancel_order
	(
		const FIX::Symbol& symbol,
		const FIX::ClOrdID& cl_ord_id,
		const FIX::OrderID& order_id,
		const FIX::Side& side)
	{
		FIX44::OrderCancelRequest cancel(
			FIX::OrigClOrdID(cl_ord_id),
			FIX::ClOrdID(serial_id()),
			side,
			FIX::TransactTime());
		set_header_fields(cancel);
		cancel.set(order_id);
		cancel.set(symbol);
		cancel.set(FIX::Account(account));
		FIX::Session::sendToTarget(cancel); }

class state{
	public:
	char * symbol;
	float bid,offer,position,average;
	WINDOW *w; };


void print_market(const FIX::Symbol& symbol, const struct state& s){
	if
		(s.position==0)
		wattron(s.w,COLOR_PAIR(1));
		else if
			(
				(s.position>0 && s.bid>s.average)
				|| (s.position<0 && s.offer<s.average)
			)wattron(s.w,COLOR_PAIR(2));
			else wattron(s.w,COLOR_PAIR(3));
	mvwprintw(s.w,0,0,"%s % 4.0f@%-7.5g  %7.5g-%-7.5g ",s.symbol,s.position,s.average,s.bid,s.offer);
	wrefresh(s.w); }

void print_equity(WINDOW * equity_window, double balance, std::map<FIX::Symbol,state> states){
	for(std::map<FIX::Symbol,struct state>::iterator it=states.begin();it!=states.end();++it)
		if(strncmp(it->second.symbol,"USD",3))
			if(it->second.position>0) balance+= multiplier*it->second.position*(it->second.bid-it->second.average);
			else balance+= multiplier*it->second.position*(it->second.offer-it->second.average);
		else
			if(it->second.position>0) balance+= multiplier*it->second.position*(it->second.bid-it->second.average)/it->second.bid;
			else balance+= multiplier*it->second.position*(it->second.offer-it->second.average)/it->second.offer;
	mvwprintw(equity_window,0,34,"%.2lf ",balance);
	wrefresh(equity_window); }

class order{
	public:
	unsigned int cl_ord_id, ord_id;
	char symbol[SYMBOL_MAX+1];
	int q;
	float p; };

int order_orders(const void * a, const void * b){
	if
		( (*(struct order **)a)->ord_id > (*(struct order **)b)->ord_id )
		return 1;
		else if
			( (*(struct order **)a)->ord_id < (*(struct order **)b)->ord_id )
			return -1;
	return 0; }

/* only the "orderstatus", "new", "trade", " and "cancel" execution report handlers modify the internal order book.  everyone else just sends requests. */

struct order ** orders=NULL;
unsigned int n_orders=0, order_book_alloc_size=0;
WINDOW * orders_window;
WINDOW *ticker_window;
char ticker[]="                                                                                ";
int control_fd;

void sigio(int signal){
	char command,symbol[8],buf[32];
	int count,q;
	double p;
	while((count=read(control_fd,&buf,31))>0){
		buf[count]='\0';
		if(sscanf(buf,"%c%7s %d@%lg",&command,symbol,&q,&p)!=4){
			fprintf(stderr,"could not parse: %s\n",buf);
			return; }
		send_order(FIX::Symbol(symbol),q,p,serial_id()); }}

void print_orders(){
	unsigned int i;
	for(i=0;i<n_orders;i++){ /*fprintf(stderr,"%u %u %s %d %g\n",orders[i]->cl_ord_id,orders[i]->ord_id,orders[i]->symbol,orders[i]->q,orders[i]->p);*/ mvwprintw(
		orders_window,
		i,0,
		"%u %s % 3d@%-7.5g",
		orders[i]->ord_id,
		orders[i]->symbol,
		orders[i]->q,
		orders[i]->p); }
	//fprintf(stderr,"\n");
	wrefresh(orders_window); }

void print_ticker(){
	mvwprintw(ticker_window,0,0,"%s",ticker);
	wrefresh(ticker_window); }

void add_order_to_internal_book
	(
		const unsigned int cl_ord_id,
		const unsigned int ord_id,
		const char * const symbol,
		const int q,
		const float p
	){
		struct order * key = new struct order();
		key->ord_id=ord_id;
		if(bsearch(&key,orders,n_orders,sizeof(struct order *),order_orders)){
			delete key;
			return; }
		delete key;
		if(n_orders+1>order_book_alloc_size){
			if
				(order_book_alloc_size==0)
				order_book_alloc_size=1;
				else order_book_alloc_size*=exp(1);
			orders=(struct order **)realloc(orders,order_book_alloc_size*sizeof(struct order *)); }
		orders[n_orders]=new struct order;
		orders[n_orders]->cl_ord_id=cl_ord_id;
		orders[n_orders]->ord_id=ord_id;
		strcpy(orders[n_orders]->symbol,symbol);
		orders[n_orders]->q=q;
		orders[n_orders]->p=p;
		n_orders++;
		qsort(orders,n_orders,sizeof(struct order *),order_orders); }

void remove_order_from_internal_book(const unsigned int cl_ord_id){
	unsigned int i;
	fprintf(stderr,"removing %u\n",cl_ord_id);
	for(i=0;i<n_orders;i++) if(orders[i]->cl_ord_id==cl_ord_id){
		fprintf(stderr,"found order at %u\n",i);
		delete orders[i];
		n_orders--;
		if(i!=n_orders){
			orders[i]=orders[n_orders];
			qsort(orders,n_orders,sizeof(struct order *),order_orders); }}}

class Fixation : public FIX::Application, public FIX::MessageCracker{

	public:

	unsigned char logged_in,cache_dirty;
	unsigned int n_position_reports, n_position_reports_received;
	std::map<FIX::Symbol,state> states;
	FIX::Symbol refresh_symbol;
	WINDOW *equity_window;
	double balance;

	Fixation() : Application() { logged_in=0; }

	bool whether_symbol(FIX::Symbol symbol){
		std::map<FIX::Symbol,struct state>::iterator it=states.find(symbol);
		if(it==states.end()) return false;
		return true; }

	void onCreate(const FIX::SessionID& sessionID){}

	void toAdmin(FIX::Message& message, const FIX::SessionID& sessionID){
		message.getHeader().setField(FIX::TargetSubID(target_sub_id)); }

	void fromAdmin
		(const FIX::Message& message, const FIX::SessionID& sessionID)
		throw(
			FIX::FieldNotFound,
			FIX::IncorrectDataFormat,
			FIX::IncorrectTagValue,
			FIX::RejectLogon) {}

	void onLogon(const FIX::SessionID& sessionID){
		std::cerr << "Logon - " << sessionID << std::endl;

		FIX44::UserRequest user_request(
			std::string("login_user_request"),
			FIX::UserRequestType_LOGONUSER,
			FIX::Username(username));
		user_request.set(FIX::Password(password));
		set_header_fields(user_request);
		FIX::Session::sendToTarget(user_request); }

	void onLogout(const FIX::SessionID& sessionID){
		logged_in=0;
		std::cerr << "Logout - " << sessionID << std::endl;
		}

	void fromApp
		(
			const FIX::Message& message,
			const FIX::SessionID& sessionID )
		throw(
			FIX::FieldNotFound,
			FIX::IncorrectDataFormat,
			FIX::IncorrectTagValue,
			FIX::UnsupportedMessageType )
		{
			crack(message, sessionID);
			//std::cout << std::endl << "IN: " << message << std::endl;
			 }

	void toApp
		(
			FIX::Message& message,
			const FIX::SessionID& sessionID )
		throw( FIX::DoNotSend )
		{

			set_header_fields(message);
			try
				{
					FIX::PossDupFlag possDupFlag;
					message.getHeader().getField(possDupFlag);
					if(possDupFlag) throw FIX::DoNotSend(); }
				catch( FIX::FieldNotFound&){}
			//std::cout << std::endl << "OUT: " << message << std::endl;
			 }

	void onMessage(const FIX44::UserResponse&, const FIX::SessionID&){
		cache_dirty=1;
		n_position_reports_received=0;
		n_position_reports=UINT_MAX;
		logged_in=1; }

	void onMessage
		(
			const FIX44::ExecutionReport& execution_report,
			const FIX::SessionID& session
		){
			long _q,d;
			static char temp[81];
			FIX::Symbol symbol;
			execution_report.get(symbol);
			if(!whether_symbol(symbol)) return;
			FIX::ClOrdID cl_ord_id;
			execution_report.get(cl_ord_id);
			FIX::OrderID order_id;
			execution_report.get(order_id);
			FIX::Side sd;
			execution_report.get(sd);
			FIX::CumQty q;
			execution_report.get(q);
			_q=q/multiplier;
			d=lmin(labs(states[symbol].position),_q);
			FIX::AvgPx p;
			execution_report.get(p);
			FIX::ExecType exec_type;
			execution_report.get(exec_type);
			//std::cerr << "received exectype " << exec_type << " clorid " << cl_ord_id << std::endl;
			switch(exec_type){
				case FIX::ExecType_NEW: {
					FIX::OrdType ot;
					execution_report.get(ot);
					if(ot==FIX::OrdType_MARKET) return; }
				case FIX::ExecType_ORDERSTATUS: {
					FIX::LeavesQty lq;
					execution_report.get(lq);
					add_order_to_internal_book(
						strtol(std::string(cl_ord_id).c_str(),NULL,10),
						strtol(std::string(order_id).c_str(),NULL,10),
						std::string(symbol).c_str(),
						(sd==FIX::Side_BUY)?lq/multiplier*1.0:-lq/multiplier,
						p+0.0);
					//std::cout << "order id " << order_id << std::endl;
					print_orders();
					break; }
				case FIX::ExecType_TRADE:{
					if(sd==FIX::Side_SELL){
						if
							(states[symbol].position>0)
							{
								_q-=d;
								states[symbol].position-=d; } 
						if(states[symbol].position<=0&&_q>0){
							states[symbol].average=(_q*p-states[symbol].average*states[symbol].position)/(_q-states[symbol].position);
							states[symbol].position-=_q; }}
					else if(sd==FIX::Side_BUY){
						if
							(states[symbol].position<0)
							{
								_q-=d;
								states[symbol].position+=d; }
						if(states[symbol].position>=0&&_q>0){
							states[symbol].average=(_q*p+states[symbol].average*states[symbol].position)/(_q+states[symbol].position);
							states[symbol].position+=_q; }}
					strncpy(temp,&ticker[19],60);
					sprintf(&temp[60],"  %s %2.0g@%-7.5g",std::string(symbol).c_str(),(sd==FIX::Side_BUY)?q/multiplier:-q/multiplier,p*1.0);
					strcpy(ticker,temp);
					print_ticker();
					remove_order_from_internal_book(strtol(std::string(cl_ord_id).c_str(),NULL,10));
					print_orders();
					break; }
				case FIX::ExecType_REJECTED:
					std::cout << cl_ord_id << " rejected" << std::endl;
					break;
				case FIX::ExecType_CANCELED:
					FIX::OrigClOrdID orig_cl_ord_id;
					execution_report.get(orig_cl_ord_id);
					remove_order_from_internal_book(strtol(std::string(orig_cl_ord_id).c_str(),NULL,10));
					print_orders();
					//std::cout << orig_cl_ord_id << " canceled" << std::endl;
					}}
				
	void onMessage
		(
			const FIX44::MarketDataSnapshotFullRefresh& message,
			const FIX::SessionID& session)
		{
			int i;
			FIX::Symbol symbol;
			message.get(symbol);
			FIX::NoMDEntries count;
			message.get(count);
			FIX44::MarketDataSnapshotFullRefresh::NoMDEntries group;
			FIX::MDEntryType type;
			FIX::MDEntryPx price;
			for(i=1;i<=count;i++){
				message.getGroup(i,group);
				group.get(type);
				group.get(price);
				if(type==FIX::MDEntryType_BID){
					states[symbol].bid=price;
					//std::cout << symbol << " market " << price << " bid" <<std::endl;
					}	
				if(type==FIX::MDEntryType_OFFER){
					states[symbol].offer=price;
					//std::cout << symbol << " market at " << price << std::endl;
					} }
			print_market(symbol,states[symbol]); }

	void onMessage
		(
			const FIX44::RequestForPositionsAck& ack,
			const FIX::SessionID& session
		){
			FIX::TotalNumPosReports count;
			ack.get(count);
			n_position_reports=count;
			} 

	void onMessage
		(
			 const FIX44::PositionReport& position_report,
			 const FIX::SessionID& session
		){
			int i;
			//FIX::PosReqID pos_req_id;
			//position_report.get(pos_req_id);
			//if(pos_req_id!=FIX::PosReqID("rfp")) return;
			FIX::Symbol report_symbol;
			position_report.get(report_symbol);
			FIX::SettlPrice sp;
			position_report.get(sp);
			FIX::NoPositions count;
			position_report.get(count);
			FIX44::PositionReport::NoPositions position_group;
			FIX::LongQty longvolume;
			FIX::ShortQty shortvolume;
			for(i=1;i<=count;i++){
				position_report.getGroup(i,position_group);
				if(position_group.isSet(longvolume)){
					position_group.get(longvolume);
					states[report_symbol].average=(states[report_symbol].average*states[report_symbol].position+sp*longvolume/multiplier)/(states[report_symbol].position+longvolume/multiplier);
					states[report_symbol].position+=(long)(longvolume/multiplier); }
				if(position_group.isSet(shortvolume)){
					position_group.get(shortvolume);
					states[report_symbol].average=(sp*shortvolume/multiplier-states[report_symbol].average*states[report_symbol].position)/(shortvolume/multiplier-states[report_symbol].position);
					states[report_symbol].position-=(long)(shortvolume/multiplier); }}
		if(++n_position_reports_received==n_position_reports)
			cache_dirty=0; }

	void onMessage(const FIX44::CollateralInquiryAck&, const FIX::SessionID&){}

	void onMessage
		(
			 const FIX44::CollateralReport& collateral_report,
			 const FIX::SessionID& session
		){
			FIX::CashOutstanding ev;
			collateral_report.get(ev);
			balance=ev; }

};


int main(int argc, char ** argv){
	unsigned int i;
	struct state * s;
	FIX::Symbol *symbol;
	Fixation f;
	FIX::SynchronizedApplication sf(f);
	initscr();
	start_color();
	init_pair(1,COLOR_WHITE,COLOR_BLACK);
	init_pair(2,COLOR_GREEN,COLOR_BLACK);
	init_pair(3,COLOR_RED,COLOR_BLACK);
	f.equity_window=newwin(1,0,0,0);
	wattron(f.equity_window,COLOR_PAIR(1));
	orders_window=newwin(22,34,1,45);
	wattron(orders_window,COLOR_PAIR(1));
	ticker_window=newwin(1,0,23,0);
	wattron(ticker_window,COLOR_PAIR(1));
	signal(SIGIO,sigio);
	mkfifo("/tmp/liquidity.ctl",0600);
	if
		(
			(control_fd=open("/tmp/liquidity.ctl",O_RDONLY|O_NONBLOCK))==-1
			|| fcntl(control_fd,F_SETFL,O_ASYNC|O_NONBLOCK)
			|| fcntl(control_fd,F_SETOWN,getpid()) )
		DIE;

	for(i=0;(int)i<(argc-2);i++){
		s=new struct state;
		symbol=new FIX::Symbol(argv[2+i]);
		s->symbol=argv[2+i];
		s->w=newwin(1,36,1+i,0);
		f.states.insert(std::pair<FIX::Symbol,struct state>(*symbol,*s)); } 

	srand(time(NULL));
	FIX::SessionSettings settings(argv[1]);
	read_settings(argv[1],sender_comp_id,target_comp_id,target_sub_id,username,password,account);
	FIX::FileStoreFactory data_store(settings);
	FIX::FileLogFactory log_factory(settings);
	FIX::SocketInitiator socket_initiator(sf,data_store,settings,log_factory);
	//FIX::SocketInitiator socket_initiator(sf,data_store,settings);
	socket_initiator.start();

	FIX44::MarketDataRequest market_data_request(
		FIX::MDReqID("mdr"),
		FIX::SubscriptionRequestType(FIX::SubscriptionRequestType_UNSUBSCRIBE),
		FIX::MarketDepth(0));
	FIX44::MarketDataRequest::NoMDEntryTypes market_data_entry_group;
	market_data_entry_group.set(FIX::MDEntryType(FIX::MDEntryType_BID));
	market_data_request.addGroup(market_data_entry_group);
	for(std::map<FIX::Symbol,struct state>::iterator it=f.states.begin();it!=f.states.end();++it){
		FIX44::MarketDataRequest::NoRelatedSym symbol_group;
		symbol_group.set(it->first);
		market_data_request.addGroup(symbol_group); }
	set_header_fields(market_data_request);

	while(1){
		while(!f.logged_in) sleep(1);
		if(f.cache_dirty){
			clear();
			for(i=0;i<n_orders;i++) delete orders[i];
			n_orders=0;
			for(std::map<FIX::Symbol,struct state>::iterator it=f.states.begin();it!=f.states.end();++it){
				it->second.bid=0;
				it->second.offer=0;
				it->second.position=0;
				it->second.average=0;
				}

			FIX44::OrderMassStatusRequest order_status_request(
				FIX::MassStatusReqID("osr"),
				FIX::MassStatusReqType(FIX::MassStatusReqType_STATUS_FOR_ALL_ORDERS));
			set_header_fields(order_status_request);
			FIX::Session::sendToTarget(order_status_request);

			FIX44::RequestForPositions request_for_positions(
				FIX::PosReqID(serial_id()),
				FIX::PosReqType(FIX::PosReqType_POSITIONS),
				FIX::Account(account),
				FIX::AccountType(FIX::AccountType_ACCOUNT_IS_CARRIED_ON_NON_CUSTOMER_SIDE_OF_BOOKS_AND_IS_CROSS_MARGINED),
				FIX::ClearingBusinessDate(),
				FIX::TransactTime());
			set_header_fields(request_for_positions);
			request_for_positions.set(FIX::Account(account));
			FIX::Session::sendToTarget(request_for_positions);
			FIX44::CollateralInquiry ci;
			ci.set(FIX::CollInquiryID("cid"));
			ci.set(FIX::SubscriptionRequestType(FIX::SubscriptionRequestType_UNSUBSCRIBE));
			set_header_fields(ci);
			FIX::Session::sendToTarget(ci);
			ci.set(FIX::SubscriptionRequestType(FIX::SubscriptionRequestType_SNAPSHOT_PLUS_UPDATES));
			FIX::Session::sendToTarget(ci);
			while(f.logged_in&&f.cache_dirty) sleep(1);
			}
		if(!f.logged_in) continue;
		FIX::Session::sendToTarget(market_data_request);
		market_data_request.set(FIX::SubscriptionRequestType(FIX::SubscriptionRequestType_SNAPSHOT));
		FIX::Session::sendToTarget(market_data_request);
		market_data_request.set(FIX::SubscriptionRequestType(FIX::SubscriptionRequestType_UNSUBSCRIBE));
		print_equity(f.equity_window,f.balance,f.states);
		sleep(3); }

	socket_initiator.stop();
	return 0;
}

/* IN GOD WE TRVST */
