// $Id: pet.c,v 1.4 2004/09/25 05:32:18 MouseJstr Exp $
#include "db.h"
#include "timer.h"
#include "socket.h"
#include "nullpo.h"
#include "malloc.h"
#include "pc.h"
#include "status.h"
#include "map.h"
#include "intif.h"
#include "clif.h"
#include "chrif.h"
#include "pet.h"
#include "itemdb.h"
#include "battle.h"
#include "mob.h"
#include "npc.h"
#include "script.h"
#include "skill.h"
#include "showmsg.h"
#include "utils.h"

int pet_attack(struct pet_data &pd,unsigned int tick,int data);
int petskill_castend(struct pet_data &pd,unsigned long tick, struct castend_delay *dat);
int petskill_castend2(struct pet_data &pd, struct block_list &target, unsigned short skill_id, unsigned short skill_lv, unsigned short skill_x, unsigned short skill_y, unsigned long tick);
int pet_attackskill(struct pet_data &pd, unsigned long tick, int data);


#define MIN_PETTHINKTIME 100

struct petdb pet_db[MAX_PET_DB];

const static char dirx[8] = { 0, 1, 1, 1, 0,-1,-1,-1};
const static char diry[8] = { 1, 1, 0,-1,-1,-1, 0, 1};



int pet_data::walktimer_func_old(int tid, unsigned long tick, basics::numptr data)
{
	struct pet_data *pd=this;

	if(pd->block_list::prev == NULL)
		return 1;

	switch(pd->state.state){
		case MS_WALK:
			pd->walk(tick);
			break;
		case MS_ATTACK:
			if (pd->msd == NULL) //Is this even possible?
				break;
			if (pc_isdead(*pd->msd))
			{	//Stop attacking when master died.
				pet_stopattack(*pd);
				break;
			}
			if (pd->state.casting_flag) 
			{	//There is a skill being cast.
				petskill_castend(*pd, tick, data.isptr?((struct castend_delay *)data.ptr):NULL );
				struct TimerData *td = get_timer(tid);
				if(td && td->data.isptr)
				{
					td->data = 0;
				}
				break;
			}
			if (battle_config.pet_status_support &&
				pd->a_skill &&
				(!battle_config.pet_equip_required || pd->equip_id > 0) &&
				(rand()%100 < (pd->a_skill->rate +pd->msd->pet.intimate*pd->a_skill->bonusrate/1000))
				)
			{	//Skotlex: Use pet's skill 
				pet_attackskill(*pd,tick,data.num);
				break;
			}
			pet_attack(*pd,tick,data.num);
			break;
		case MS_DELAY:
			pd->changestate(MS_IDLE,0);
			break;
		default:
			if(battle_config.error_log)
				ShowMessage("pet_timer : %d ?\n",pd->state.state);
			break;
	}

	return 0;
}
/*==========================================
 *
 *------------------------------------------
 */
int pet_data::walkstep_old(unsigned long tick)
{
	int moveblock;
	int i;
	int x,y,dx,dy;

	this->state.state=MS_IDLE;
	if( this->walkpath.finished() )
		return 0;

	this->walkpath.path_half ^= 1;
	if(this->walkpath.path_half==0)
	{
		this->walkpath++;
		if(this->walkpath.change_target)
		{
			this->walktoxy_sub();
			return 0;
		}
	}
	else
	{
		x = this->block_list::x;
		y = this->block_list::y;

		this->dir=this->walkpath.get_current_step();
		dx = dirx[this->dir];
		dy = diry[this->dir];

		if(map_getcell(this->block_list::m,x+dx,y+dy,CELL_CHKNOPASS)){
			this->walktoxy_sub();
			return 0;
		}

		moveblock = ( x/BLOCK_SIZE != (x+dx)/BLOCK_SIZE || y/BLOCK_SIZE != (y+dy)/BLOCK_SIZE);

		this->state.state=MS_WALK;

		CMap::foreachinmovearea( CClifPetOutsight(*this),
			this->block_list::m,x-AREA_SIZE,y-AREA_SIZE,x+AREA_SIZE,y+AREA_SIZE,dx,dy,BL_PC);

		x += dx;
		y += dy;

		if(moveblock) this->map_delblock();
		this->block_list::x = x;
		this->block_list::y = y;
		if(moveblock) this->map_addblock();

		CMap::foreachinmovearea( CClifPetInsight(*this),
			this->block_list::m,x-AREA_SIZE,y-AREA_SIZE,x+AREA_SIZE,y+AREA_SIZE,-dx,-dy,BL_PC);

		this->state.state=MS_IDLE;
	}
	if((i=this->calc_next_walk_step())>0)
	{
		i = i>>1;
		if(i < 1 && this->walkpath.path_half == 0)
			i = 1;

		this->state.state=MS_WALK;
		if(this->walktimer!=-1)
		{
			struct TimerData *td = get_timer(this->walktimer);
			if(td && td->data.isptr)
			{
				delete ((struct castend_delay*)td->data.ptr);
				td->data = 0;
			}
			delete_timer(this->walktimer, movable::walktimer_entry_old);
		}
		this->walktimer=add_timer(tick+i,movable::walktimer_entry_old,this->block_list::id,0);
	}
	if( this->walkpath.finished() )
		clif_fixobject(*this);

	return 0;
}

int pet_data::walktoxy_sub_old()
{
	if( !this->walkpath.path_search(this->block_list::m,this->block_list::x,this->block_list::y,this->target.x,this->target.y,0) )
		return 1;
	this->walkpath.change_target=0;
	this->changestate(MS_WALK,0);
	clif_moveobject(*this);
	return 0;
}

int pet_data::walktoxy_old(unsigned short x,unsigned short y, bool easy)
{
	if(this->state.state == MS_WALK && !walkpath_data::is_possible(this->block_list::m,this->block_list::x,this->block_list::y,x,y,0))
		return 1;

	this->target.x=x;
	this->target.y=y;

	if(this->state.state == MS_WALK) {
		this->walkpath.change_target=1;
	} else {
		return this->walktoxy_sub();
	}
	return 0;
}

int pet_data::stop_walking_old(int type)
{
	if(this->state.state == MS_WALK || this->state.state == MS_IDLE)
	{
		this->walkpath.clear();
		this->target.x=this->block_list::x;
		this->target.y=this->block_list::y;
	}
	if(type&0x01)
		clif_fixobject(*this);
	if(type&~0xff)
		this->changestate(MS_DELAY,type>>8);
	else
		this->changestate(MS_IDLE,0);
	return 0;
}

int pet_data::changestate_old(int state,int type)
{
	pet_data &pd = *this;
	unsigned long tick;
	int i;

	if( pd.state.casting_flag )
		skill_castcancel(&pd, 0);

	if(pd.walktimer != -1)
	{
		struct TimerData *td = get_timer(pd.walktimer);
		if(td && td->data.isptr)
		{
			delete ((struct castend_delay*)td->data.ptr);
			td->data = 0;
		}
		delete_timer(pd.walktimer,movable::walktimer_entry_old);
		pd.walktimer=-1;
	}
	pd.state.state=state;

	switch(state)
	{
		case MS_WALK:
			if((i=pd.calc_next_walk_step()) > 0){
				i = i>>2;
				pd.walktimer=add_timer(gettick()+i,movable::walktimer_entry_old,pd.block_list::id,0);
			} else
				pd.state.state=MS_IDLE;
			break;
		case MS_ATTACK:
			tick = gettick();
			i=DIFF_TICK(pd.attackable_tick,tick);
			if(i>0 && i<2000)
				pd.walktimer=add_timer(pd.attackable_tick,movable::walktimer_entry_old,pd.block_list::id,0);
			else
				pd.walktimer=add_timer(tick+1,movable::walktimer_entry_old,pd.block_list::id,0);
			break;
		case MS_DELAY:
				pd.walktimer=add_timer(gettick()+type,movable::walktimer_entry_old,pd.block_list::id,0);
			break;
	}
	return 0;
}



/// do object depending stuff for ending the walk.
void pet_data::do_stop_walking()
{
	if(this->state.state == MS_WALK)
		this->changestate(MS_IDLE,0);
//	if(type&~0xff)
//		this->changestate(MS_DELAY,type>>8);
//	else
//		this->changestate(MS_IDLE,0);
}
/// do object depending stuff for the walk step.
void pet_data::do_walkstep(unsigned long tick, const coordinate &target, int dx, int dy)
{
	this->state.state=MS_WALK;
}
/// do object depending stuff for changestate
void pet_data::do_changestate(int state,int type)
{
	pet_data &pd = *this;
	unsigned long tick;
	int i;

	if( pd.state.casting_flag )
		skill_castcancel(&pd, 0);

	if(pd.walktimer != -1)
	{
		struct TimerData *td = get_timer(pd.walktimer);
		if(td && td->data.isptr)
		{
			delete ((struct castend_delay*)td->data.ptr);
			td->data = 0;
		}

		if( pd.is_walking() )
			delete_timer(pd.walktimer,pd.walktimer_entry);
		else
			delete_timer(pd.walktimer,movable::walktimer_entry_old);
		pd.walktimer=-1;
	}
	pd.state.state=state;

	switch(state)
	{
		case MS_WALK:
			if( !pd.set_walktimer( gettick() ) )
				pd.state.state=MS_IDLE;
			break;
		case MS_ATTACK:
			tick = gettick();
			i=DIFF_TICK(pd.attackable_tick,tick);
			if(i>0 && i<2000)
				pd.walktimer=add_timer(pd.attackable_tick,movable::walktimer_entry_old,pd.block_list::id,0);
			else
				pd.walktimer=add_timer(tick+1,movable::walktimer_entry_old,pd.block_list::id,0);
			break;
		case MS_DELAY:
				pd.walktimer=add_timer(gettick()+type,movable::walktimer_entry_old,pd.block_list::id,0);
			break;
	}
}
// Random walk
int pet_data::randomwalk_old(unsigned long tick)
{
	pet_data& pd = *this;
	const int retrycount=20;
	int speed = pd.get_speed();

	if(DIFF_TICK(pd.next_walktime,tick) < 0){
		int i,x,y,d=12-pd.move_fail_count;
		if(d<5) d=5;
		for(i=0;i<retrycount;++i)
		{
			int r=rand();
			if(pd.msd && distance(pd, *pd.msd)<d )
			{	// use master coordinates as base for random walk
				x=pd.msd->block_list::x+r%(d*2+1)-d;
				y=pd.msd->block_list::y+r/(d*2+1)%(d*2+1)-d;
			}
			else
			{
				x=pd.block_list::x+r%(d*2+1)-d;
				y=pd.block_list::y+r/(d*2+1)%(d*2+1)-d;
			}

			if((map_getcell(pd.block_list::m,x,y,CELL_CHKPASS))&&( pd.walktoxy(x,y)==0)){
				pd.move_fail_count=0;
				break;
			}
			if(i+1>=retrycount){
				pd.move_fail_count++;
				if(pd.move_fail_count>1000){
					if(battle_config.error_log)
						ShowMessage("PET cant move. hold position %d, class_ = %d\n",pd.block_list::id,pd.class_);
					pd.move_fail_count=0;
					pd.changestate(MS_DELAY,60000);
					return 0;
				}
			}
		}

		pd.next_walktime = tick+rand()%3000+3000+speed*pd.walkpath.get_path_time()/10;
		return 1;
	}
	return 0;
}





// Appearance income of mob
// actually a copy from mob but until pet and mob don't have a real heritage
// the best is to have both classes completely seperated
int pet_data::get_viewclass() const
{
	return mob_db[this->class_].view_class;
}
int pet_data::get_sex() const
{
	return mob_db[this->class_].sex;
}
ushort pet_data::get_hair() const
{
	return mob_db[this->class_].hair;
}
ushort pet_data::get_hair_color() const
{
	return mob_db[this->class_].hair_color;
}
ushort pet_data::get_weapon() const
{
	return mob_db[this->class_].weapon;
}
ushort pet_data::get_shield() const
{
	return mob_db[this->class_].shield;
}
ushort pet_data::get_head_top() const
{
	return mob_db[this->class_].head_top;
}
ushort pet_data::get_head_mid() const
{
	return mob_db[this->class_].head_mid;
}
ushort pet_data::get_head_buttom() const
{
	return mob_db[this->class_].head_buttom;
}
ushort pet_data::get_clothes_color() const
{
	return mob_db[this->class_].clothes_color;
}
int pet_data::get_equip() const
{
	return mob_db[this->class_].equip;
}






int pet_performance_val(struct map_session_data &sd)
{
	if(sd.pet.intimate > 900 && sd.petDB)
		return (sd.petDB->s_perfor > 0)? 4:3;
	else if(sd.pet.intimate > 750)
		return 2;
	else
		return 1;
}

int pet_hungry_val(struct map_session_data &sd)
{
	if(sd.pet.hungry > 90)
		return 4;
	else if(sd.pet.hungry > 75)
		return 3;
	else if(sd.pet.hungry > 25)
		return 2;
	else if(sd.pet.hungry > 10)
		return 1;
	else
		return 0;
}


int pet_calc_pos(struct pet_data &pd,int tx,int ty,int dir)
{
	int x,y,dx,dy;
	int i,k;
	uint32 vary = rand();

	dir+=vary&0x03-1;	// vary the target position
	dir &= 0x07;		// only 3 bits allowed

	dx = -dirx[dir]*((vary&0x04)!=0)?2:1;
	dy = -diry[dir]*((vary&0x08)!=0)?2:1;

	x = tx + dx;
	y = ty + dy;
	if( !pd.can_reach(x,y) )
	{	// bunch of unnecessary code
		if(dx > 0) x--;
		else if(dx < 0) x++;
		if(dy > 0) y--;
		else if(dy < 0) y++;
		if( !pd.can_reach(x,y) )
		{
			for(i=0;i<12;++i)
			{
				k = rand()%8;
				dx = -dirx[k]*2;
				dy = -diry[k]*2;
				x = tx + dx;
				y = ty + dy;
				if( pd.can_reach(x,y) )
					break;
				else
				{
					if(dx > 0) x--;
					else if(dx < 0) x++;
					if(dy > 0) y--;
					else if(dy < 0) y++;
					if( pd.can_reach(x,y) )
						break;
				}
			}
			if(i>=12)
			{
				x = tx;
				y = ty;
				if( pd.can_reach(x,y) )
					return 1;
			}
		}
	}

	pd.target.x = x;
	pd.target.y = y;
	return 0;
}

int pet_unlocktarget(struct pet_data &pd)
{
	pd.target_id=0;
	return 0;
}

int pet_attack(struct pet_data &pd,unsigned int tick,int datax)
{
	struct mob_data *md;
	short range;

	md=(struct mob_data *)map_id2bl(pd.target_id);
	if(md == NULL || pd.block_list::m != md->block_list::m || md->block_list::prev == NULL ||
		distance(pd, *md) > 13)
	{
		pet_unlocktarget(pd);
		return 0;
	}

	range = mob_db[pd.class_].range + 1;
	if(distance(pd, *md) > range)
		return 0;
	if(battle_config.monster_attack_direction_change)
		pd.dir=pd.get_direction(*md);

	clif_fixobject(pd);

	pd.target_lv = battle_weapon_attack(&pd,md,tick,0);

	pd.attackable_tick = tick + status_get_adelay(&pd);

	if( pd.walktimer!=-1 )
		pd.changestate(MS_IDLE,0);

	pd.walktimer=add_timer(pd.attackable_tick,movable::walktimer_entry_old,pd.block_list::id,0);
	pd.state.state=MS_ATTACK;
	return 0;
}



/*==========================================
 * Pet Attack Skill [Skotlex]
 *------------------------------------------
 */
int pet_attackskill(struct pet_data &pd, unsigned long tick, int data)
{

	struct block_list *bl;
	bl = map_id2bl(pd.target_id);
	if(bl == NULL || pd.block_list::m != bl->m || bl->prev == NULL ||
		NULL==pd.a_skill ||
		distance(pd,*bl) > 13)
	{
		pet_unlocktarget(pd);
		return 0;
	}
	petskill_use(pd, *bl, pd.a_skill->id, pd.a_skill->lv, tick);
	return 0;
}

/*==========================================
 * Pet Skill Use [Skotlex]
 *------------------------------------------
 */
int petskill_use(struct pet_data &pd, struct block_list &target, short skill_id, short skill_lv, unsigned int tick)
{
	int casttime;

	if(pd.state.casting_flag)
		return 1;	//Will not interrupt an already casting skill.

	if( pd.is_walking() )	//Cancel whatever else the pet is doing.
		pd.changestate(MS_IDLE,0);

	
	if(battle_config.monster_attack_direction_change)
		pd.dir=pd.get_direction(target);
	clif_fixobject(pd);

	//Casting time
	casttime=skill_castfix(&pd, skill_get_cast(skill_id, skill_lv));
		
	pd.stop_walking(1);
	pd.attackable_tick = tick;
	pd.state.state=MS_ATTACK;

	if (casttime > 0)
	{
		pd.attackable_tick += casttime;
		pd.state.state=MS_ATTACK;
		pd.state.casting_flag = 1;

		if (skill_get_inf(skill_id) & INF_GROUND_SKILL)
			clif_skillcasting(pd, pd.block_list::id, 0, target.x, target.y, skill_id, casttime);
		else
			clif_skillcasting(pd, pd.block_list::id, target.id, 0, 0, skill_id,casttime);
		
		struct castend_delay *dat
			= new struct castend_delay(pd, target.id, skill_id, skill_lv, 0);
		pd.walktimer = add_timer(pd.attackable_tick,movable::walktimer_entry_old,pd.block_list::id, basics::numptr(dat), false);
	}
	else
	{
		petskill_castend2(pd, target, skill_id, skill_lv, target.x, target.y, tick);
	}	
	return 0;
}

/*==========================================
 * Pet Attack Cast End [Skotlex]
 *------------------------------------------
 */
int petskill_castend(struct pet_data &pd,unsigned long tick, struct castend_delay *dat)
{
	if(dat)
	{
		struct block_list *target = map_id2bl(dat->target_id);
		pd.state.state = MS_IDLE;
		pd.state.casting_flag = 0;
		if (target && dat->src.id == pd.block_list::id && target->prev != NULL)
			petskill_castend2(pd, *target, dat->skill_id, dat->skill_lv, target->x, target->y, tick);
		delete dat;
	}
	return 0;
}

/*==========================================
 * Pet Attack Cast End2 [Skotlex]
 *------------------------------------------
 */
int petskill_castend2(struct pet_data &pd, struct block_list &target, unsigned short skill_id, unsigned short skill_lv, unsigned short skill_x, unsigned short skill_y, unsigned long tick)
{	//Invoked after the casting time has passed.
	short delaytime =0, range;

	pd.state.state=MS_IDLE;
	
	if (skill_get_inf(skill_id) & INF_GROUND_SKILL)
	{	//Area skill
		skill_castend_pos2(&pd, skill_x, skill_y, skill_id, skill_lv, tick,0);
	} else { //Targeted Skill
		//Skills with inf = 4 (cast on self) have view range (assumed party skills)
		range = (skill_get_inf(skill_id) & INF_SELF_SKILL?battle_config.area_size:skill_get_range(skill_id, skill_lv));
		if(range < 0)
			range = status_get_range(&pd) - (range + 1);
		if(distance(pd, target) > range)
			return 0; 
		switch( skill_get_nk(skill_id) )
		{
			case NK_NO_DAMAGE:
				if(
				(skill_id==AL_HEAL || skill_id==ALL_RESURRECTION) && battle_check_undead(status_get_race(&target),status_get_elem_type(&target)) )
					skill_castend_damage_id(&pd, &target, skill_id, skill_lv, tick, 0);
				else
				{
				  skill_castend_nodamage_id(&pd,&target, skill_id, skill_lv,tick, 0);
				}
				break;
			case NK_SPLASH_DAMAGE:
			default:
				skill_castend_damage_id(&pd,&target,skill_id,skill_lv,tick,0);
				break;
		}
	}

	if (pd.walktimer != -1) //The above skill casting could had changed the state (Abracadabra?)
		return 0;

	delaytime = skill_delayfix(&pd,skill_get_delay(skill_id, skill_lv));
	if (delaytime < MIN_PETTHINKTIME)
		delaytime = status_get_adelay(&pd);
	pd.attackable_tick = tick + delaytime; 
	if (pd.target_id) //Resume attacking
		pd.state.state=MS_ATTACK;
	pd.walktimer=add_timer(pd.attackable_tick,movable::walktimer_entry_old,pd.block_list::id,0);

	return 0;
}



int pet_stopattack(struct pet_data &pd)
{
	pd.target_id=0;
	if(pd.state.state == MS_ATTACK)
		pd.changestate(MS_IDLE,0);

	return 0;
}

int pet_target_check(struct map_session_data &sd,struct block_list *bl,int type)
{
	struct pet_data *pd;
	struct mob_data *md;
	int rate,mode,race;

	pd = sd.pd;
	if( bl && pd && bl->type == BL_MOB && 
		sd.pet.intimate >= (short)battle_config.pet_support_min_friendly &&
		sd.pet.hungry >= 1 &&
		pd->class_ != status_get_class(bl) &&
		pd->state.state != MS_DELAY )
	{
		mode = mob_db[pd->class_].mode;
		race = mob_db[pd->class_].race;

		md=(struct mob_data *)bl;

		if(pd->block_list::m != md->block_list::m ||
			distance(*pd, *md) > 13 || 
			(md->class_ >= 1285 && md->class_ <= 1288)) // Cannot attack Guardians/Emperium
			return 0;

		if(mob_db[pd->class_].mexp <= 0 && !(mode&0x20) && (md->option & 0x06 && race!=4 && race!=6) )
			return 0;
		
		if(!type)
		{
			rate = sd.petDB->attack_rate;
			rate = rate * pd->rate_fix/1000;
			if(sd.petDB->attack_rate > 0 && rate <= 0)
				rate = 1;
		} else {
			rate = sd.petDB->defence_attack_rate;
			rate = rate * pd->rate_fix/1000;
			if(sd.petDB->defence_attack_rate > 0 && rate <= 0)
				rate = 1;
		}
		if(rand()%10000 < rate)
		{
			if(pd->target_id == 0 || rand()%10000 < sd.petDB->change_target_rate)
				pd->target_id = bl->id;
		}
	}
	return 0;
}
/*==========================================
 * Pet SC Check [Skotlex]
 *------------------------------------------
 */
int pet_sc_check(struct map_session_data &sd, int type)
{	
	struct pet_data *pd;

	pd = sd.pd;
	if (pd == NULL ||
		(battle_config.pet_equip_required && pd->equip_id == 0) ||
		pd->recovery == NULL ||
		pd->recovery->timer != -1 ||
		pd->recovery->type != type)
		return 1;

	pd->recovery->timer = add_timer(gettick()+pd->recovery->delay*1000,pet_recovery_timer,sd.block_list::id,0);
	
	return 0;
}




int pet_hungry(int tid, unsigned long tick, int id, basics::numptr data)
{
	int interval, t;

	struct map_session_data *sd=map_id2sd(id);
	if(sd==NULL)
		return 1;

	if(sd->pet_hungry_timer != tid){
		if(battle_config.error_log)
			ShowMessage("pet_hungry_timer %d != %d\n",sd->pet_hungry_timer,tid);
		return 0;
	}
	
	sd->pet_hungry_timer = -1;

	if(!sd->status.pet_id || !sd->pd || !sd->petDB)
		return 1;

	sd->pet.hungry--;
	
	// remind owner to feed the pet
	// "*~ Original code by Kitty, adapted by flaviojs ~*"
	if( sd->pet.hungry <= 35 && (sd->pet.hungry <= 25 || sd->pet.hungry%2) )
	{
		const static unsigned char petemicon_list[] = 
		{
			 7,	// Smoke cloud (angry) /ag
			16,	// Wah (crying) /wah
			43,	// Obsessed 2 (eyes popping) /e8 or /slur
			37,	// Obsessed 1 (drooling) /e2 or /rice
			20	// Hmm /hmm			 
		};
		unsigned char i = (sd->pet.hungry) ? ((sd->pet.hungry+4)/10+1) : 0;
		clif_pet_emotion(*sd->pd, petemicon_list[i%(sizeof(petemicon_list)/sizeof(petemicon_list[0]))]);
	}


	t = sd->pet.intimate;
	if(sd->pet.hungry < 0) {
		if(sd->pd->target_id > 0)
			pet_stopattack(*(sd->pd));
		sd->pet.hungry = 0;
		sd->pet.intimate -= battle_config.pet_hungry_friendly_decrease;
		if(sd->pet.intimate <= 0) {
			sd->pet.intimate = 0;
			if(battle_config.pet_status_support && t > 0) {
				if(sd->block_list::prev != NULL)
					status_calc_pc(*sd,0);
				else
					status_calc_pc(*sd,2);
			}
		}
		status_calc_pet(*sd, 0);
		clif_send_petdata(*sd,1,sd->pet.intimate);
	}
	clif_send_petdata(*sd,2,sd->pet.hungry);

	if(battle_config.pet_hungry_delay_rate != 100)
		interval = (sd->petDB->hungry_delay*battle_config.pet_hungry_delay_rate)/100;
	else
		interval = sd->petDB->hungry_delay;
	if(interval <= 0)
		interval = 1;
	sd->pet_hungry_timer = add_timer(tick+interval,pet_hungry,sd->block_list::id,0);

	return 0;
}

int search_petDB_index(int key,int type)
{
	int i;

	for(i=0;i<MAX_PET_DB;++i) {
		if(pet_db[i].class_ <= 0)
			continue;
		switch(type) {
			case PET_CLASS:
				if(pet_db[i].class_ == key)
					return i;
				break;
			case PET_CATCH:
				if(pet_db[i].itemID == key)
					return i;
				break;
			case PET_EGG:
				if(pet_db[i].EggID == key)
					return i;
				break;
			case PET_EQUIP:
				if(pet_db[i].AcceID == key)
					return i;
				break;
			case PET_FOOD:
				if(pet_db[i].FoodID == key)
					return i;
				break;
			default:
				return -1;
		}
	}
	return -1;
}


int pet_remove_map(struct map_session_data &sd)
{
	if(sd.status.pet_id && sd.pd)
	{
		struct pet_data *pd=sd.pd; // [Valaris]
		//[Skotlex] clear bonus data
		skill_cleartimerskill(pd);
		if(pd->status)
		{
			delete pd->status;
			pd->status = NULL;
		}
		if (pd->a_skill)
		{
			delete pd->a_skill;
			pd->a_skill = NULL;
		}
		if (pd->s_skill)
		{
			if (pd->s_skill->timer != -1)
			{
				if (pd->s_skill->id == 0)
					delete_timer(pd->s_skill->timer, pet_heal_timer);
				else
					delete_timer(pd->s_skill->timer, pet_skill_support_timer);
			}
			delete pd->s_skill;
			pd->s_skill = NULL;
		}
		if(pd->recovery)
		{
			if(pd->recovery->timer != -1)
				delete_timer(pd->recovery->timer, pet_recovery_timer);
			delete pd->recovery;
			pd->recovery = NULL;
		}
		if(pd->bonus)
		{
			if (pd->bonus->timer != -1)
				delete_timer(pd->bonus->timer, pet_skill_bonus_timer);
			delete pd->bonus;
			pd->bonus = NULL;
		}
		if (pd->loot)
		{
			if (pd->loot->item)
				delete pd->loot->item;
			delete pd->loot;
			pd->loot = NULL;
		}
		pd->state.skillbonus=-1;
		sd.state.perfect_hiding=0;	// end additions

		sd.pd->changestate(MS_IDLE,0);

		if(sd.pet_hungry_timer != -1) {
			delete_timer(sd.pet_hungry_timer,pet_hungry);
			sd.pet_hungry_timer = -1;
		}

		clif_clearchar_area(*sd.pd,0);
		sd.pd->map_delblock();

		sd.pd->map_deliddb();
		sd.pd->map_freeblock();
		sd.pd = NULL;
	}
	return 0;
}

int pet_performance(struct map_session_data &sd)
{
	nullpo_retr(0, sd.pd);
	struct pet_data &pd = *sd.pd;

	pd.stop_walking(2000<<8);
	clif_pet_performance(pd,rand()%pet_performance_val(sd) + 1);
	// ルートしたItemを落とさせる
	pet_lootitem_drop(pd,NULL);

	return 0;
}

int pet_return_egg(struct map_session_data &sd)
{
	struct item tmp_item;
	int flag;

	if(sd.status.pet_id && sd.pd) {

		// ルートしたItemを落とさせる
		pet_lootitem_drop(*(sd.pd),&sd);

		pet_remove_map(sd);
		sd.status.pet_id = 0;
		sd.pd = NULL;
		if(sd.petDB == NULL)
			return 1;

		sd.pet.incuvate = 1;

		tmp_item = (struct item)( sd.petDB->EggID );
		tmp_item.identify = 1;
		tmp_item.card[0] = 0xff00;
		tmp_item.card[1] = basics::GetWord(sd.pet.pet_id,0);
		tmp_item.card[2] = basics::GetWord(sd.pet.pet_id,1);
		tmp_item.card[3] = sd.pet.rename_flag;
		flag = pc_additem(sd,tmp_item,1);
		if( flag )
		{
			clif_additem(sd,0,0,flag);
			map_addflooritem(tmp_item,1,sd.block_list::m,sd.block_list::x,sd.block_list::y,NULL,NULL,NULL,0);
		}
		if(battle_config.pet_status_support && sd.pet.intimate > 0)
		{
			if(sd.block_list::prev != NULL)
				status_calc_pc(sd,0);
			else
				status_calc_pc(sd,2);
		}

		
		intif_save_petdata(sd.status.account_id,sd.pet);
		pc_makesavestatus(sd);
		chrif_save(sd);
		storage_storage_save(sd);

		sd.pet.rename_flag = 0;
		sd.petDB = NULL;
	}

	return 0;
}

int pet_data_init(struct map_session_data &sd)
{
	struct pet_data *pd;
	int i=0,interval=0;

	if(sd.status.account_id != sd.pet.account_id || sd.status.char_id != sd.pet.char_id ||
		sd.status.pet_id != sd.pet.pet_id) {
		sd.status.pet_id = 0;
		return 1;
	}

	i = search_petDB_index(sd.pet.class_,PET_CLASS);
	if(i < 0) {
		sd.status.pet_id = 0;
		return 1;
	}
	sd.petDB = &pet_db[i];
	sd.pd = pd = new pet_data(sd.pet.name);

	pd->block_list::m = sd.block_list::m;
	pd->block_list::x = pd->target.x = sd.block_list::x;
	pd->block_list::y = pd->target.y = sd.block_list::y;
	pet_calc_pos(*pd,sd.block_list::x,sd.block_list::y,sd.dir);
	pd->block_list::x = pd->target.x;
	pd->block_list::y = pd->target.y;
	pd->block_list::id = npc_get_new_npc_id();
	pd->class_ = sd.pet.class_;
	pd->equip_id = sd.pet.equip_id;
	pd->dir = sd.dir;
	pd->speed = sd.petDB->speed;
	pd->block_list::subtype = MONS;
	pd->block_list::type = BL_PET;
	pd->state.state = MS_IDLE;
	pd->next_walktime = pd->attackable_tick = pd->last_thinktime = gettick();
	pd->msd = &sd;
	
	for(i=0;i<MAX_MOBSKILLTIMERSKILL;++i)
		pd->skilltimerskill[i].timer = -1;

	pd->map_addiddb();

	// initialise
	if (battle_config.pet_lv_rate)	//[Skotlex]
	{
		pd->status = new pet_data::pet_status;
	}
	status_calc_pet(sd,1);

	pd->state.skillbonus = -1;
	if (battle_config.pet_status_support) //Skotlex
		CScriptEngine::run(pet_db[i].script,0,sd.block_list::id,0);

	if(battle_config.pet_hungry_delay_rate != 100)
		interval = (sd.petDB->hungry_delay*battle_config.pet_hungry_delay_rate)/100;
	else
		interval = sd.petDB->hungry_delay;
	if(interval <= 0)
		interval = 1;

	if(sd.pet_hungry_timer != -1) {
		delete_timer(sd.pet_hungry_timer,pet_hungry);
		sd.pet_hungry_timer = -1;
	}
	sd.pet_hungry_timer = add_timer(gettick()+interval,pet_hungry,sd.block_list::id,0);

	return 0;
}

int pet_birth_process(struct map_session_data &sd)
{

	if(sd.status.pet_id && sd.pet.incuvate == 1) {
		sd.status.pet_id = 0;
		return 1;
	}

	sd.pet.incuvate = 0;
	sd.pet.account_id = sd.status.account_id;
	sd.pet.char_id = sd.status.char_id;
	sd.status.pet_id = sd.pet.pet_id;
	if(pet_data_init(sd)) {
		sd.status.pet_id = 0;
		sd.pet.incuvate = 1;
		sd.pet.account_id = 0;
		sd.pet.char_id = 0;
		return 1;
	}

	intif_save_petdata(sd.status.account_id,sd.pet);
	pc_makesavestatus(sd);
	chrif_save(sd);
	storage_storage_save(sd);
	sd.pd->map_addblock();
	clif_spawnpet(*sd.pd);
	clif_send_petdata(sd,0,0);
	clif_send_petdata(sd,5,battle_config.pet_hair_style);
	clif_pet_equip(*sd.pd,sd.pet.equip_id);
	clif_send_petstatus(sd);

	return 0;
}

int pet_recv_petdata(uint32 account_id, struct petstatus &p,int flag)
{
	struct map_session_data *sd;

	sd = map_id2sd(account_id);
	if(sd == NULL)
		return 1;
	if(flag == 1) {
		sd->status.pet_id = 0;
		return 1;
	}
	memcpy(&sd->pet,&p,sizeof(struct petstatus));
	if(sd->pet.incuvate == 1)
		pet_birth_process(*sd);
	else {
		pet_data_init(*sd);
		if(sd->pd && sd->block_list::prev != NULL) {
			sd->pd->map_addblock();
			clif_spawnpet(*sd->pd);
			clif_send_petdata(*sd,0,0);
			clif_send_petdata(*sd,5,battle_config.pet_hair_style);
//			clif_pet_equip(*sd->pd,sd->pet.equip);
			clif_send_petstatus(*sd);
		}
	}
	if(battle_config.pet_status_support && sd->pet.intimate > 0) {
		if(sd->block_list::prev != NULL)
			status_calc_pc(*sd,0);
		else
			status_calc_pc(*sd,2);
	}

	return 0;
}

int pet_select_egg(struct map_session_data &sd,short egg_index)
{
	if(sd.status.inventory[egg_index].card[0] == 0xff00)
	{
		intif_request_petdata(sd.status.account_id, sd.status.char_id, basics::MakeDWord(sd.status.inventory[egg_index].card[1], sd.status.inventory[egg_index].card[2]) );
		pc_delitem(sd,egg_index,1,0);
	}
	else {
		if(battle_config.error_log)
			ShowMessage("wrong egg item inventory %d\n",egg_index);
	}
	return 0;
}

int pet_catch_process1(struct map_session_data &sd,int target_class)
{
	sd.catch_target_class = target_class;
	clif_catch_process(sd);

	return 0;
}

int pet_catch_process2(struct map_session_data &sd,uint32 target_id)
{
	struct mob_data *md;
	int pet_catch_rate=0;

	if( sd.itemindex >=MAX_INVENTORY ||
		sd.status.inventory[sd.itemindex].nameid != sd.itemid ||
		!sd.inventory_data[sd.itemindex]->flag.delay_consume ||
		sd.status.inventory[sd.itemindex].amount < 1 )
	{	//Something went wrong, items moved or they tried an exploit.
		clif_pet_rulet(sd,0);
		sd.catch_target_class = -1;
		return 1;
	}
	//Consume the pet lure [Skotlex]
	pc_delitem(sd,sd.itemindex,1,0);
	sd.itemid = sd.itemindex = 0xFFFF;
	
	md=(struct mob_data*)map_id2bl(target_id);
	if(!md || md->block_list::type != BL_MOB || md->block_list::prev == NULL){
		clif_pet_rulet(sd,0);
		sd.catch_target_class = -1;
		return 1;
	}

	int i = search_petDB_index(md->class_,PET_CLASS);
	//catch_target_class == 0 is used for universal lures. [Skotlex]
	//for now universal lures do not include bosses.
	if (sd.catch_target_class == 0 && !(md->mode&0x20))
		sd.catch_target_class = md->class_;
	if(i < 0 || i>=MAX_PET_DB || sd.catch_target_class != md->class_) {
		clif_emotion(*md, 7);	//mob will do /ag if wrong lure is used on them.
		clif_pet_rulet(sd,0);
		sd.catch_target_class = -1;
		return 1;
	}

	//target_idによる敵→卵判定
//	if(battle_config.etc_log)
//		ShowMessage("mob_id = %d, mob_class = %d\n",md->block_list::id,md->class_);
		//成功の場合
	pet_catch_rate = (pet_db[i].capture + (sd.status.base_level - mob_db[md->class_].lv)*30 + sd.paramc[5]*20)*(200 - md->hp*100/mob_db[md->class_].max_hp)/100;
	if(pet_catch_rate < 1) pet_catch_rate = 1;
	if(battle_config.pet_catch_rate != 100)
		pet_catch_rate = (pet_catch_rate*battle_config.pet_catch_rate)/100;

	if(rand()%10000 < pet_catch_rate)
	{
		mob_remove_map(*md,0);
		clif_pet_rulet(sd,1);
//		if(battle_config.etc_log)
//			ShowMessage("rulet success %d\n",target_id);
		intif_create_pet(
			sd.status.account_id,sd.status.char_id,
			pet_db[i].class_,mob_db[pet_db[i].class_].lv,
			pet_db[i].EggID,0,pet_db[i].intimate,100,
			0,1,
			pet_db[i].jname);
	}
	else
	{
		sd.catch_target_class = -1;
		clif_pet_rulet(sd,0);
	}

	return 0;
}

int pet_get_egg(uint32 account_id,uint32 pet_id,int flag)
{
	struct map_session_data *sd;
	struct item tmp_item;
	int i=0,ret=0;

	if(!flag) {
		sd = map_id2sd(account_id);
		if(sd == NULL)
			return 1;

		i = search_petDB_index(sd->catch_target_class,PET_CLASS);
		sd->catch_target_class = -1;
		
		if(i >= 0) {
			tmp_item = (struct item)( pet_db[i].EggID );
			tmp_item.identify = 1;
			tmp_item.card[0] = 0xff00;
			tmp_item.card[1] = basics::GetWord(pet_id,0);
			tmp_item.card[2] = basics::GetWord(pet_id,1);
			tmp_item.card[3] = sd->pet.rename_flag;
			if((ret = pc_additem(*sd,tmp_item,1))) {
				clif_additem(*sd,0,0,ret);
				map_addflooritem(tmp_item,1,sd->block_list::m,sd->block_list::x,sd->block_list::y,NULL,NULL,NULL,0);
			}
		}
		else
			intif_delete_petdata(pet_id);
	}

	return 0;
}

int pet_menu(struct map_session_data &sd,int menunum)
{
	if (sd.pd == NULL)
		return 1;
	switch(menunum)
	{
		case 0:
			clif_send_petstatus(sd);
			break;
		case 1:
			pet_food(sd);
			break;
		case 2:
			pet_performance(sd);
			break;
		case 3:
			pet_return_egg(sd);
			break;
		case 4:
			pet_unequipitem(sd);
			break;
	}
	return 0;
}

int pet_change_name(struct map_session_data &sd, const char *name)
{
	int i;
	

	if((sd.pd == NULL) || (sd.pet.rename_flag == 1 && battle_config.pet_rename == 0))
		return 1;

	for(i=0;i<24 && name[i];++i){
		if( !(name[i]&0xe0) || name[i]==0x7f)
			return 1;
	}

	sd.pd->stop_walking(1);
	memcpy(sd.pet.name,name,24);
	//memcpy(sd->pd->name,name,24);
	clif_clearchar_area(*sd.pd,0);
	clif_spawnpet(*sd.pd);
	clif_send_petdata(sd,0,0);
	clif_send_petdata(sd,5,battle_config.pet_hair_style);
	sd.pet.rename_flag = 1;
	clif_pet_equip(*sd.pd,sd.pet.equip_id);
	clif_send_petstatus(sd);

	return 0;
}

int pet_equipitem(struct map_session_data &sd,int index)
{
	unsigned short nameid;

	nameid = sd.status.inventory[index].nameid;
	if(sd.petDB == NULL)
		return 1;
	if(sd.petDB->AcceID == 0 || nameid != sd.petDB->AcceID || sd.pet.equip_id != 0)
	{
		clif_equipitemack(sd,0,0,0);
		return 1;
	}
	else
	{
		pc_delitem(sd,index,1,0);
		sd.pet.equip_id = sd.pd->equip_id = nameid;
		status_calc_pc(sd,0);
		clif_pet_equip(*sd.pd,nameid);
		if (battle_config.pet_equip_required)
		{ 	//Skotlex: start support timers if needd
			if (sd.pd->s_skill && sd.pd->s_skill->timer == -1)
			{
				if (sd.pd->s_skill->id)
					sd.pd->s_skill->timer=add_timer(gettick()+sd.pd->s_skill->delay*1000, pet_skill_support_timer, sd.block_list::id, 0);
				else
					sd.pd->s_skill->timer=add_timer(gettick()+sd.pd->s_skill->delay*1000, pet_heal_timer, sd.block_list::id, 0);
			}
			if (sd.pd->bonus && sd.pd->bonus->timer == -1)
				sd.pd->bonus->timer=add_timer(gettick()+sd.pd->bonus->delay*1000, pet_skill_bonus_timer, sd.block_list::id, 0);
		}
	}
	return 0;
}

int pet_unequipitem(struct map_session_data &sd)
{
	struct item tmp_item;
	unsigned short nameid;
	int flag;

	if(sd.petDB == NULL)
		return 1;
	if(sd.pet.equip_id == 0)
		return 1;

	nameid = sd.pet.equip_id;
	sd.pet.equip_id = sd.pd->equip_id = 0;
	status_calc_pc(sd,0);
	clif_pet_equip(*sd.pd,0);

	tmp_item = (struct item)( nameid );
	tmp_item.identify = 1;
	if((flag = pc_additem(sd,tmp_item,1))) {
		clif_additem(sd,0,0,flag);
		map_addflooritem(tmp_item,1,sd.block_list::m,sd.block_list::x,sd.block_list::y,NULL,NULL,NULL,0);
	}
	if (battle_config.pet_equip_required)
	{ 	//Skotlex: halt support timers if needed
		if (sd.pd->s_skill && sd.pd->s_skill->timer != -1)
		{
			if (sd.pd->s_skill->id == 0)
				delete_timer(sd.pd->s_skill->timer, pet_heal_timer);
			else
				delete_timer(sd.pd->s_skill->timer, pet_skill_support_timer);
			sd.pd->s_skill->timer=-1;
		}
		if(sd.pd->bonus && sd.pd->bonus->timer != -1)
		{
			delete_timer(sd.pd->bonus->timer, pet_skill_bonus_timer);
			sd.pd->bonus->timer = -1;
		}
	}

	return 0;
}

int pet_food(struct map_session_data &sd)
{
	int i,k,t;

	if(sd.petDB == NULL)
		return 1;
	i=pc_search_inventory(sd,sd.petDB->FoodID);
	if(i < 0) {
		clif_pet_food(sd,sd.petDB->FoodID,0);
		return 1;
	}
	pc_delitem(sd,i,1,0);
	t = sd.pet.intimate;
	if(sd.pet.hungry > 90)
		sd.pet.intimate -= sd.petDB->r_full;
	else if(sd.pet.hungry > 75) {
		if(battle_config.pet_friendly_rate != 100)
			k = (sd.petDB->r_hungry * battle_config.pet_friendly_rate)/100;
		else
			k = sd.petDB->r_hungry;
		k = k >> 1;
		if(k <= 0)
			k = 1;
		sd.pet.intimate += k;
	}
	else {
		if(battle_config.pet_friendly_rate != 100)
			k = (sd.petDB->r_hungry * battle_config.pet_friendly_rate)/100;
		else
			k = sd.petDB->r_hungry;
		sd.pet.intimate += k;
	}
	if(sd.pet.intimate <= 0) {
		sd.pet.intimate = 0;
		if(battle_config.pet_status_support && t > 0) {
			if(sd.block_list::prev != NULL)
				status_calc_pc(sd,0);
			else
				status_calc_pc(sd,2);
		}
	}
	else if(sd.pet.intimate > 1000)
		sd.pet.intimate = 1000;
	status_calc_pet(sd, 0);
	sd.pet.hungry += sd.petDB->fullness;
	if(sd.pet.hungry > 100)
		sd.pet.hungry = 100;

	clif_send_petdata(sd,2,sd.pet.hungry);
	clif_send_petdata(sd,1,sd.pet.intimate);
	clif_pet_food(sd,sd.petDB->FoodID,1);

	return 0;
}


/*
int pet_ai_sub_hard_lootsearch(struct block_list &bl,va_list &ap)
{
	struct pet_data* pd;
	int dist,*itc;

	nullpo_retr(0, ap);
	pd=va_arg(ap,struct pet_data*);
	nullpo_retr(0, pd);
	nullpo_retr(0, itc=va_arg(ap,int *));

	if(!pd->target_id){
		flooritem_data *fitem = (flooritem_data *)&bl;
		struct map_session_data *sd = NULL;
		// ルート権無し
		if(fitem && fitem->first_get_id>0)
			sd = map_id2sd(fitem->first_get_id);

		if(pd->loot == NULL || pd->loot->item == NULL || (pd->loot->count >= pd->loot->max) || (sd && sd->pd != pd))
			return 0;
		if(bl.m == pd->block_list::m && (dist=distance(pd->block_list::x,pd->block_list::y,bl.x,bl.y))<5){
			if( pd->can_reach(bl.x,bl.y)		// 到達可能性判定
				 && rand()%1000<1000/(++(*itc)) ){	// 範囲内PCで等確率にする
				pd->target_id=bl.id;
			}
		}
	}
	return 0;
}
*/
class CPetAiHardLootsearch : public CMapProcessor
{
	struct pet_data& pd;
public:
	mutable int itc;
	CPetAiHardLootsearch(struct pet_data& p) : pd(p), itc(0)	{}
	~CPetAiHardLootsearch()	{}
	virtual int process(struct block_list& bl) const
	{
		int dist;
		if( !pd.target_id )
		{
			flooritem_data *fitem = (flooritem_data *)&bl;
			struct map_session_data *sd = NULL;
			// ルート権無し
			if(fitem && fitem->first_get_id>0)
				sd = map_id2sd(fitem->first_get_id);

			if( pd.loot == NULL || pd.loot->item == NULL || (pd.loot->count >= pd.loot->max) || 
				(sd && sd->pd && sd->pd->block_list::id != pd.block_list::id) )
				return 0;
			if(bl.m == pd.block_list::m && (dist=distance(pd.block_list::x,pd.block_list::y,bl.x,bl.y))<5)
			{
				if( pd.can_reach(bl.x,bl.y) &&		// 到達可能性判定
					rand()%1000<1000/(++itc) )			// 範囲内PCで等確率にする
				{	
					pd.target_id=bl.id;
				}
			}
		}
		return 0;
	}
};
int pet_ai_sub_hard(struct pet_data &pd, unsigned long tick)
{
	struct map_session_data *sd = pd.msd;
	struct mob_data *md = NULL;
	int dist,i=0,dx,dy,ret;
	int mode,race;

	if( pd.block_list::prev == NULL || sd == NULL || sd->block_list::prev == NULL )
		return 0;

	if( sd->status.pet_id == 0 || sd->pd == NULL || sd->pd->msd != sd)
		return 0;

	if(DIFF_TICK(tick,pd.last_thinktime) < MIN_PETTHINKTIME)
		return 0;

	pd.last_thinktime=tick;

	if(pd.state.state == MS_DELAY || pd.block_list::m != sd->block_list::m)
		return 0;

	// ペットによるルート
	if(!pd.target_id && pd.loot && pd.loot->count < pd.loot->max && DIFF_TICK(gettick(),pd.loot->loottick)>0)
	{
		CPetAiHardLootsearch pal(pd);

		CMap::foreachinarea( pal,
			pd.block_list::m, ((int)pd.block_list::x)-AREA_SIZE/2,((int)pd.block_list::y)-AREA_SIZE/2, ((int)pd.block_list::x)+AREA_SIZE/2,((int)pd.block_list::y)+AREA_SIZE/2,BL_ITEM);
		i=pal.itc;
//		i=0;
//		map_foreachinarea(pet_ai_sub_hard_lootsearch,
//			pd.block_list::m, ((int)pd.block_list::x)-AREA_SIZE/2,((int)pd.block_list::y)-AREA_SIZE/2, ((int)pd.block_list::x)+AREA_SIZE/2,((int)pd.block_list::y)+AREA_SIZE/2,BL_ITEM,
//			&pd,&i);
	}

	if(sd->pet.intimate > 0)
	{
		dist = distance(*sd,pd);
		if(dist > 12)
		{
			if(pd.target_id > 0)
				pet_unlocktarget(pd);
			if( pd.is_walking() && sd->get_distance(pd.target.x,pd.target.y) < 3)
				return 0;
			pd.speed = sd->speed *3/4; // be faster than master
			if( pd.speed <= 10 )
				pd.speed = 10;
			pet_calc_pos(pd,sd->block_list::x,sd->block_list::y,sd->dir);
			if( pd.walktoxy(pd.target.x,pd.target.y) )
				pd.randomwalk(tick);
		}
		else if(pd.target_id > MAX_FLOORITEM)
		{	//Mob targeted
			mode=mob_db[pd.class_].mode;
			race=mob_db[pd.class_].race;
			md=(struct mob_data *)map_id2bl(pd.target_id);
			if( md == NULL /*|| md->block_list::type != BL_MOB*/ || 
				pd.block_list::m != md->block_list::m || md->block_list::prev == NULL ||
				distance(pd, *md) > 13)
			{
				pet_unlocktarget(pd);
			}
//			else if(mob_db[pd->class_].mexp <= 0 && !(mode&0x20) && (md->option & 0x06 && race!=4 && race!=6) )
//				pet_unlocktarget(pd);
			else if(!battle_check_range(&pd,md,mob_db[pd.class_].range && !pd.state.casting_flag))
			{	//Skotlex Don't interrupt a casting spell when targed moved
				if( pd.is_walking() && md->get_distance(pd.target.x,pd.target.y) < 2)
					return 0;
				if( !pd.can_reach(md->block_list::x,md->block_list::y) )
					pet_unlocktarget(pd);
				else
				{
					i=0;
					pd.calc_speed();
					do
					{
						if(i==0)
						{	// 最初はAEGISと同じ方法で検索
							dx=md->block_list::x - pd.block_list::x;
							dy=md->block_list::y - pd.block_list::y;
							if(dx<0) dx++;
							else if(dx>0) dx--;
							if(dy<0) dy++;
							else if(dy>0) dy--;
						}
						else
						{	// だめならAthena式(ランダム)
							dx=md->block_list::x - pd.block_list::x + rand()%3 - 1;
							dy=md->block_list::y - pd.block_list::y + rand()%3 - 1;
						}
						ret=pd.walktoxy(pd.block_list::x+dx,pd.block_list::y+dy);
						i++;
					} while(ret && i<5);

					if(ret)
					{	// 移動不可能な所からの攻撃なら2歩下る
						if(dx<0) dx=2;
						else if(dx>0) dx=-2;
						if(dy<0) dy=2;
						else if(dy>0) dy=-2;
						pd.walktoxy(pd.block_list::x+dx,pd.block_list::y+dy);
					}
				}
			}
			else
			{
				if(pd.state.state==MS_WALK)
					pd.stop_walking(1);
				if(pd.state.state==MS_ATTACK)
					return 0;
				pd.changestate(MS_ATTACK,0);
			}
		}
		else if(pd.target_id > 0 && pd.loot)
		{	//Item Targeted, attempt loot
			struct block_list *bl_item = map_id2bl(pd.target_id);
			if( bl_item == NULL || bl_item->type != BL_ITEM || bl_item->m != pd.block_list::m || 
				(dist=distance(pd,*bl_item))>=5 )
			{
				 // 遠すぎるかアイテムがなくなった
 				pet_unlocktarget(pd);
			}
			else if(dist)
			{
				if( pd.walktimer != -1 && pd.state.state!=MS_ATTACK && (DIFF_TICK(pd.next_walktime,tick)<0 || bl_item->get_distance(pd.target.x,pd.target.y) <= 0))
					return 0; // 既に移動中

				pd.next_walktime=tick+500;
				//dx=bl_item->x - pd.block_list::x;
				//dy=bl_item->y - pd.block_list::y;
				//ret=pd.walktoxy(pd->block_list::x+dx,pd->block_list::y+dy);
				ret=pd.walktoxy( bl_item->x, bl_item->y);
			}
			else
			{	// アイテムまでたどり着いた
				if(pd.state.state==MS_ATTACK)
					return 0; // 攻撃中

				if(pd.state.state==MS_WALK)
				{	// 歩行中なら停止
					pd.stop_walking(1);
				}

				if(pd.loot && pd.loot->count < pd.loot->max)
				{
					flooritem_data *fitem = (flooritem_data *)bl_item;
					memcpy(&pd.loot->item[pd.loot->count++], &fitem->item_data, sizeof(struct item));
					pd.loot->weight += itemdb_search(fitem->item_data.nameid)->weight*fitem->item_data.amount;
					map_clearflooritem(bl_item->id);
				}
					pet_unlocktarget(pd);
				}
			}
		else
		{
			if( pd.is_walking() && sd->get_distance(pd.target.x,pd.target.y) < 3 )
				return 0;
			if(dist<=3)
			{
				if(battle_config.pet_random_move && !pc_issit(*sd) )
					pd.randomwalk(tick);
				return 0;
			}
			pd.calc_speed();
			pet_calc_pos(pd,sd->block_list::x,sd->block_list::y,sd->dir);
			if(pd.walktoxy(pd.target.x,pd.target.y))
				pd.randomwalk(tick);
		}
	}
	else
	{
		pd.calc_speed();
		if(pd.state.state == MS_ATTACK)
			pet_stopattack(pd);
		pd.randomwalk(tick);
	}
	return 0;
}


class CClifpet_ai : public CClifProcessor
{
	unsigned long tick;
public:
	CClifpet_ai(unsigned long t) : tick(t)	{}
	virtual ~CClifpet_ai()	{}
	virtual bool process(struct map_session_data& sd) const
	{
		if(sd.status.pet_id && sd.pd && sd.petDB)
			pet_ai_sub_hard(*sd.pd, tick);
		return 0;
	}
};
int pet_ai_hard(int tid, unsigned long tick, int id, basics::numptr data)
{
	clif_foreachclient( CClifpet_ai(tick) );
	return 0;
}

int pet_lootitem_drop(struct pet_data &pd,struct map_session_data *sd)
{
	size_t i,flag=0;
	if(pd.loot)
	{	// is a looter
		for(i=0;i<pd.loot->count;++i)
		{	// 落とさないで直接PCのItem欄へ
			if(sd)
			{	// player exists
				if((flag = pc_additem(*sd,pd.loot->item[i],pd.loot->item[i].amount)))
				{	// drop items on floor
					clif_additem(*sd,0,0,flag);
					map_addflooritem(pd.loot->item[i],pd.loot->item[i].amount,pd.block_list::m, pd.block_list::x, pd.block_list::y,NULL,NULL,NULL,0);
				}
			}
			else
			{	// create a delay drop structure
				struct delay_item_drop2 *ditem;
				ditem = new struct delay_item_drop2(pd.block_list::m, pd.block_list::x, pd.block_list::y, pd.loot->item[i]);
				add_timer(gettick()+540+i,pet_delay_item_drop2,0, basics::numptr(ditem), false);
			}
		}
		pd.loot->count = 0;
		pd.loot->weight = 0;
		pd.loot->loottick = gettick()+10000;	//	10*1000msの間拾わない
	}
	return 1;
}

int pet_delay_item_drop2(int tid, unsigned long tick, int id, basics::numptr data)
{
	struct delay_item_drop2 *ditem;

	ditem=(struct delay_item_drop2 *)data.ptr;
	if(ditem)
	{
		map_addflooritem(ditem->item_data,ditem->item_data.amount,ditem->m,ditem->x,ditem->y,ditem->first_sd,ditem->second_sd,ditem->third_sd,0);
		delete ditem;
		get_timer(tid)->data=0;
	}
	return 0;
}

/*==========================================
 * pet bonus giving skills [Valaris] / Rewritten by [Skotlex]
 *------------------------------------------
 */ 
int pet_skill_bonus_timer(int tid, unsigned long tick, int id, basics::numptr data)
{
	struct map_session_data *sd=map_id2sd(id);
	struct pet_data *pd;
	int timer = 0;

	if(sd == NULL || sd->pd==NULL || sd->pd->bonus == NULL)
		return 1;
	
	pd=sd->pd;
	if(pd->bonus->timer != tid) {
		if(battle_config.error_log)
			ShowMessage("pet_skill_bonus_timer %d != %d\n",pd->bonus->timer,tid);
		return 0;
	}
	
	// determine the time for the next timer
	if (pd->state.skillbonus == 0) {
		// pet bonuses are not active at the moment, so,
		pd->state.skillbonus = 1;
		timer = pd->bonus->duration*1000;	// the duration for pet bonuses to be in effect
	} else if (pd->state.skillbonus == 1) {
		// pet bonuses are already active, so,
		pd->state.skillbonus = 0;
		timer = (pd->bonus->delay - pd->bonus->duration)*1000;	// the duration until pet bonuses will be reactivated again
		if (timer < 0) //Always active bonus
			timer = MIN_PETTHINKTIME; 
	}

	// add/remove our bonuses
	status_calc_pc(*sd, 0);

	// wait for the next timer
	pd->bonus->timer=add_timer(tick+timer,pet_skill_bonus_timer,sd->block_list::id,0);
	
	return 0;
}

int pet_recovery_timer(int tid, unsigned long tick, int id, basics::numptr data)
{
	struct map_session_data *sd=(struct map_session_data*)map_id2bl(id);
	struct pet_data *pd;
	
	if(sd==NULL || sd->pd == NULL || sd->pd->recovery == NULL)
		return 1;
	
	pd=sd->pd;

	if(pd->recovery == NULL || pd->recovery->timer != tid) {
		if(battle_config.error_log)
		{
			if (pd->recovery)
				ShowMessage("pet_recovery_timer %d != %d\n",pd->recovery->timer,tid);
			else
				ShowMessage("pet_recovery_timer called with no recovery skill defined (tid=%d)\n",tid);
		}
		return 0;
	}

	if(sd->sc_data && sd->sc_data[pd->recovery->type].timer != -1)
	{	//Display a heal
		clif_skill_nodamage(*pd,*sd,TF_DETOXIFY,1,1);
		status_change_end(sd,pd->recovery->type,-1);
		clif_emotion(*pd, 33);
	}

	pd->recovery->timer = -1;
	
	return 0;
}

int pet_heal_timer(int tid, unsigned long tick, int id, basics::numptr data)
{
	struct map_session_data *sd=(struct map_session_data*)map_id2bl(id);
	struct pet_data *pd;
	short rate = 100;
	
	if(sd==NULL || sd->block_list::type!=BL_PC || sd->pd == NULL)
		return 1;
	
	pd=sd->pd;

	if(pd->s_skill == NULL || pd->s_skill->timer != tid) {
		if(battle_config.error_log)
		{
			if (pd->s_skill)
				ShowMessage("pet_heal_timer %d != %d\n",pd->s_skill->timer,tid);
			else
				ShowMessage("pet_heal_timer called with no support skill defined (tid=%d)\n",tid);
		}
		return 0;
	}
	
	if(pc_isdead(*sd) ||
		(rate = sd->status.sp*100/sd->status.max_sp) > pd->s_skill->sp ||
		(rate = sd->status.hp*100/sd->status.max_hp) > pd->s_skill->hp ||
		(rate = pd->state.casting_flag) || //Another skill is in effect
		(rate = pd->state.state) == MS_WALK) //Better wait until the pet stops moving (MS_WALK is 2)
	{  //Wait (how long? 1 sec for every 10% of remaining)
		pd->s_skill->timer=add_timer(gettick()+(rate>10?rate:10)*100,pet_heal_timer,sd->block_list::id,0);
		return 0;
	}

	if (pd->state.state == MS_ATTACK)
			pet_stopattack(*pd);
	clif_skill_nodamage(*pd,*sd,AL_HEAL,pd->s_skill->lv,1);
	pc_heal(*sd,pd->s_skill->lv,0);
	
	pd->s_skill->timer=add_timer(tick+pd->s_skill->delay*1000,pet_heal_timer,sd->block_list::id,0);
	
	return 0;
}

/*==========================================
 * pet support skills [Skotlex]
 *------------------------------------------
 */ 
int pet_skill_support_timer(int tid, unsigned long tick, int id, basics::numptr data)
{
	struct map_session_data *sd=(struct map_session_data*)map_id2bl(id);
	struct pet_data *pd;
	short rate = 100;	
	if(sd==NULL || sd->block_list::type!=BL_PC || sd->pd == NULL)
		return 1;
	
	pd=sd->pd;
	
	if(pd->s_skill == NULL || pd->s_skill->timer != tid) {
		if(battle_config.error_log)
		{
			if (pd->s_skill)
				ShowMessage("pet_skill_support_timer %d != %d\n",pd->s_skill->timer,tid);
			else
				ShowMessage("pet_skill_support_timer called with no support skill defined (tid=%d)\n",tid);
		}
		return 0;
	}
	
	if(pc_isdead(*sd) ||
		(rate = sd->status.sp*100/sd->status.max_sp) > pd->s_skill->sp ||
		(rate = sd->status.hp*100/sd->status.max_hp) > pd->s_skill->hp ||
		(rate = pd->state.casting_flag) || //Another skill is in effect
		(rate = pd->state.state) == MS_WALK) //Better wait until the pet stops moving (MS_WALK is 2)
	{  //Wait (how long? 1 sec for every 10% of remaining)
		pd->s_skill->timer=add_timer(gettick()+(rate>10?rate:10)*100,pet_skill_support_timer,sd->block_list::id,0);
		return 0;
	}
	
	if (pd->state.state == MS_ATTACK)
		pet_stopattack(*pd);
	petskill_use(*pd, *sd, pd->s_skill->id, pd->s_skill->lv, tick);

	pd->s_skill->timer=add_timer(tick+pd->s_skill->delay*1000,pet_skill_support_timer,sd->block_list::id,0);
	
	return 0;
}

/*==========================================
 *ペットデータ読み込み
 *------------------------------------------
 */ 
int read_petdb()
{
	FILE *fp;
	char line[1024];
	unsigned short nameid;
	size_t i,k,j=0;
	int lines;
	char *filename[]={"db/pet_db.txt","db/pet_db2.txt"};
	char *str[32],*p,*np;
	
	memset(pet_db,0,sizeof(pet_db));
	for(i=0;i<2;++i){
		fp=basics::safefopen(filename[i],"r");
		if(fp==NULL){
			if(i>0)
				continue;
			ShowError("can't read %s\n",filename[i]);
			return -1;
		}
		lines = 0;
		while(fgets(line,sizeof(line),fp)){
			
			lines++;

			if( !get_prepared_line(line) )
				continue;

			for(k=0,p=line;k<20;++k){
				if((np=strchr(p,','))!=NULL){
					str[k]=p;
					*np=0;
					p=np+1;
				} else {
					str[k]=p;
					p+=strlen(p);
				}
			}

			nameid=atoi(str[0]);
			if(nameid<=0 || nameid>2000)
				continue;
		
			//MobID,Name,JName,ItemID,EggID,AcceID,FoodID,"Fullness (1回の餌での満腹度増加率%)","HungryDeray (/min)","R_Hungry (空腹時餌やり親密度増加率%)","R_Full (とても満腹時餌やり親密度減少率%)","Intimate (捕獲時親密度%)","Die (死亡時親密度減少率%)","Capture (捕獲率%)",(Name)
			pet_db[j].class_ = nameid;
			memcpy(pet_db[j].name,str[1],24);
			memcpy(pet_db[j].jname,str[2],24);
			pet_db[j].itemID=atoi(str[3]);
			pet_db[j].EggID=atoi(str[4]);
			pet_db[j].AcceID=atoi(str[5]);
			pet_db[j].FoodID=atoi(str[6]);
			pet_db[j].fullness=atoi(str[7]);
			pet_db[j].hungry_delay=atoi(str[8])*1000;
			pet_db[j].r_hungry=atoi(str[9]);
			if(pet_db[j].r_hungry <= 0)
				pet_db[j].r_hungry=1;
			pet_db[j].r_full=atoi(str[10]);
			pet_db[j].intimate=atoi(str[11]);
			pet_db[j].die=atoi(str[12]);
			pet_db[j].capture=atoi(str[13]);
			pet_db[j].speed=atoi(str[14]);
			pet_db[j].s_perfor=(char)atoi(str[15]);
			pet_db[j].talk_convert_class=atoi(str[16]);
			pet_db[j].attack_rate=atoi(str[17]);
			pet_db[j].defence_attack_rate=atoi(str[18]);
			pet_db[j].change_target_rate=atoi(str[19]);
			pet_db[j].script = NULL;
			if((np=strchr(p,'{'))==NULL)
				continue;
			pet_db[j].script = parse_script((unsigned char *) np,lines);
			j++;
		}
		fclose(fp);
		ShowStatus("Done reading '"CL_WHITE"%d"CL_RESET"' pets in '"CL_WHITE"%s"CL_RESET"'.\n",j,filename[i]);
	}
	return 0;
}

/*==========================================
 * スキル関係初期化処理
 *------------------------------------------
 */
int do_init_pet(void)
{
	read_petdb();

	add_timer_func_list(pet_hungry,"pet_hungry");
	add_timer_func_list(pet_ai_hard,"pet_ai_hard");
	add_timer_func_list(pet_skill_bonus_timer,"pet_skill_bonus_timer"); // [Valaris]
	add_timer_func_list(pet_delay_item_drop2,"pet_delay_item_drop2");	
	add_timer_func_list(pet_skill_support_timer, "pet_skill_support_timer"); // [Skotlex]
	add_timer_func_list(pet_recovery_timer,"pet_recovery_timer"); // [Valaris]
	add_timer_func_list(pet_heal_timer,"pet_heal_timer"); // [Valaris]
	add_timer_interval(gettick()+MIN_PETTHINKTIME,MIN_PETTHINKTIME,pet_ai_hard,0,0);

	return 0;
}

int do_final_pet(void)
{
	int i;
	for(i = 0;i < MAX_PET_DB; ++i)
	{
		if(pet_db[i].script)
		{
			delete[] pet_db[i].script;
			pet_db[i].script=NULL;
		}
	}
	return 0;
}
