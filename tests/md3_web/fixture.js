const p = (name, html) => `<div class="probe" data-probe="${name}">${html}</div>`;
const h = text => `<h2>${text}</h2>`;
const row = html => `<div class="row">${html}</div>`;
const icon = (slot='') => `<md-icon ${slot ? `slot="${slot}"` : ''}>+</md-icon>`;
const buttons = disabled => ['filled', 'filled-tonal', 'outlined', 'elevated', 'text'].map(v=>`<md-${v}-button ${disabled?'disabled':''}>Button</md-${v}-button>`).join('');
const chips = disabled => ['assist','filter','input','suggestion'].map(v=>`<md-${v}-chip label="${v[0].toUpperCase()+v.slice(1)}" ${v==='filter'?'selected':''} ${v==='input'?'remove-only':''} ${disabled?'disabled':''}></md-${v}-chip>`).join('');
const controls = disabled => [
`<md-checkbox checked aria-label="Checked"></md-checkbox>`,`<md-checkbox aria-label="Unchecked"></md-checkbox>`,`<md-checkbox indeterminate aria-label="Mixed"></md-checkbox>`,
`<md-radio checked name="radio" aria-label="Selected"></md-radio>`,`<md-radio name="radio" aria-label="Unselected"></md-radio>`,
`<md-switch selected aria-label="On"></md-switch>`,`<md-switch aria-label="Off"></md-switch>`].map(x=>disabled?x.replace('name="radio"','name="radio-disabled"').replace(' aria-label',' disabled aria-label'):x).join('');
const slider = attrs => `<md-slider min="0" max="100" value="40" aria-label="Slider" ${attrs}></md-slider>`;
const field = (variant,attrs='') => `<md-${variant}-text-field label="Label" value="Input text" supporting-text="Supporting text" ${attrs}></md-${variant}-text-field>`;
const tab = variant => `<md-tabs>${['First','Second','Third'].map((v,i)=>`<md-${variant}-tab ${i===0?'active':''}>${v}</md-${variant}-tab>`).join('')}</md-tabs>`;
const select = variant => `<md-${variant}-select label="Label"><md-select-option value="first" selected><div slot="headline">First</div></md-select-option><md-select-option value="second"><div slot="headline">Second</div></md-select-option></md-${variant}-select>`;
const groups = {
 dialog: h('Basic dialog — modal') + '<md-dialog open quick id="reference-dialog"><div slot="headline">Dialog title</div><div slot="content">Supporting text</div><div slot="actions"><md-text-button>Cancel</md-text-button><md-text-button>Confirm</md-text-button></div></md-dialog>',
 actions: h('Buttons — 40 dp height, 14 sp medium label')+row('<span class="state">Enabled</span>'+p('buttons',buttons(false)))+row('<span class="state">Disabled</span>'+p('buttons-disabled',buttons(true)))+
 h('Icon buttons — standard / filled / tonal / outlined')+p('icon-buttons',['icon-button','filled-icon-button','filled-tonal-icon-button','outlined-icon-button'].map(v=>`<md-${v} aria-label="Add">${icon()}</md-${v}>`).join(''))+
 h('Floating action buttons — small / medium / large / extended')+p('fabs',['small','medium','large'].map(size=>`<md-fab size="${size}" aria-label="Add">${icon('icon')}</md-fab>`).join('')+`<md-fab label="Create">${icon('icon')}</md-fab>`)+
 h('Chips — assist / selected filter / removable input / suggestion')+p('chips',chips(false))+h('Disabled chips')+p('chips-disabled',chips(true)),
 inputs:h('Checkbox / radio / switch — on and off')+p('selection',controls(false))+h('Disabled selection controls')+p('selection-disabled',controls(true))+
 h('Sliders — continuous / discrete (0–100, value 40)')+row(p('slider',slider(''))+p('slider-discrete',slider('step="10" ticks labeled')))+row(p('slider-disabled',slider('disabled'))+p('slider-range',slider('value-start="20" value-end="80" range')))+
 h('Text fields — filled / outlined')+row(p('field-filled',field('filled'))+p('field-outlined',field('outlined')))+row(p('field-error',field('filled','error error-text="Error text"'))+p('field-disabled',field('outlined','disabled'))),
 layout:h('Cards — filled / outlined / elevated (official experimental labs)')+p('cards',['filled','outlined','elevated'].map(v=>`<md-${v}-card>${v[0].toUpperCase()+v.slice(1)} card<br><span class="caption">Supporting text</span></md-${v}-card>`).join(''))+
 h('Primary tabs')+p('tabs-primary',tab('primary'))+h('Secondary tabs')+p('tabs-secondary',tab('secondary'))+
 h('Navigation bar (official experimental labs)')+p('navigation-bar','<md-navigation-bar>'+['Home','Search','Library'].map((v,i)=>`<md-navigation-tab ${i===0?'active':''} label="${v}"><md-icon slot="active-icon">${v[0]}</md-icon><md-icon slot="inactive-icon">${v[0]}</md-icon></md-navigation-tab>`).join('')+'</md-navigation-bar>')+
 h('Lists — one, two, three lines + divider')+p('list',`<md-list><md-list-item>Headline${icon('start')}<span slot="end">100+</span></md-list-item><md-divider></md-divider><md-list-item><div slot="headline">Headline</div><div slot="supporting-text">Supporting text</div>${icon('start')}</md-list-item><md-list-item multiline-supporting-text><div slot="headline">Headline</div><div slot="supporting-text">Supporting text on two lines<br>Second line</div>${icon('start')}</md-list-item></md-list>`),
 feedback:h('Progress — determinate 40%')+row(p('progress-linear','<md-linear-progress value="0.4" aria-label="Loading"></md-linear-progress>')+p('progress-circular','<md-circular-progress value="0.4" aria-label="Loading"></md-circular-progress>'))+
 h('Segmented buttons (official experimental labs)')+p('segmented','<md-outlined-segmented-button-set>'+['First','Second','Third'].map((v,i)=>`<md-outlined-segmented-button label="${v}" ${i===0?'selected':''}></md-outlined-segmented-button>`).join('')+'</md-outlined-segmented-button-set>')+
 h('Select — filled / outlined')+row(p('select-filled',select('filled'))+p('select-outlined',select('outlined')))+h('Badges (official experimental labs)')+p('badges','<md-badge></md-badge><md-badge value="3"></md-badge><md-badge value="999+"></md-badge>')+
 h('Menu')+'<div class="popup-anchor"><md-text-button id="menu-anchor">Menu</md-text-button><md-menu anchor="menu-anchor" id="reference-menu" positioning="absolute" open><md-menu-item><div slot="headline">First</div></md-menu-item><md-menu-item><div slot="headline">Second</div></md-menu-item><md-menu-item disabled><div slot="headline">Disabled</div></md-menu-item></md-menu></div>'
};
const params = new URLSearchParams(location.search);
const group = params.get('group') || 'actions';
const dark = params.get('theme') === 'dark';
document.documentElement.classList.toggle('dark', dark);
document.querySelector('#content').innerHTML = groups[group] || groups.actions;
document.querySelector('#title').textContent = `${group[0].toUpperCase()+group.slice(1)} / ${dark?'dark':'light'}`;
window.fixtureReady = true;
