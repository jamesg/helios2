var ModalButton = Backbone.Model.extend(
    {
        defaults: {
            action: function() {},
            label: 'Button',
            name: 'close',
            icon: 'check'
        }
    }
    );

var ModalButtonView = StaticView.extend(
    {
        events: {
            'click button': 'triggerClick'
        },
        tagName: 'span',
        template: '\
        <button type="button" name="<%-name%>">\
            <span class="oi" data-glyph="<%-icon%>" aria-hidden="true"> </span>\
            <%-label%>\
        </button>\
        ',
        triggerClick: function() {
            this.model.trigger('click');
        }
    }
    );

var ModalButtonCollection = Backbone.Collection.extend(
    {
        model: ModalButton,
        comparator: function(model) {
            var index = ['no', 'cancel', 'close', 'destroy', 'save', 'yes', 'prev', 'next']
                .indexOf(model.get('name'));
            return (index > -1) ? index : model.get('name');
        }
    }
    );

var StandardButton = {
    close: function(action) {
        return { name: 'close', icon: 'x', label: 'Close', action: action };
    },
    cancel: function(action) {
        return { name: 'cancel', icon: 'x', label: 'Cancel', action: action };
    },
    destroy: function(action) {
        return { name: 'destroy', icon: 'trash', label: 'Delete', action: action };
    },
    no: function(action) {
        return { name: 'no', icon: 'x', label: 'No', action: action };
    },
    yes: function(action) {
        return { name: 'yes', icon: 'check', label: 'Yes', action: action };
    },
    create: function(action) {
        return { name: 'create', icon: 'file', label: 'Create', action: action };
    },
    ok: function(action) {
        return { name: 'ok', icon: 'check', label: 'Ok', action: action };
    },
    save: function(action) {
        return { name: 'save', icon: 'data-transfer-download', label: 'Save', action: action };
    },
    prev: function(action) {
        return { name: 'prev', icon: 'chevron-left', label: 'Prev', action: action };
    },
    next: function(action) {
        return { name: 'next', icon: 'chevron-right', label: 'Next', action: action };
    }
};

/*
 * Return the first defined, non-null argument.  Call with any number of
 * arguments.
 *
 * If no arguments are defined and non-null, return null.
 */
var coalesce = function() {
    for(a in arguments)
        if(!_.isNull(arguments[a]) && !_.isUndefined(arguments[a]))
            return arguments[a];
    return null;
};

var Modal = Backbone.View.extend(
    {
        /*
         * options - map of:
         *     buttons - buttons to display
         *     view - view constructor
         *     model - passed to the view
         *     help - view providing help, to be displayed in another modal
         */
        initialize: function(options) {
            Backbone.View.prototype.initialize.apply(this, arguments);
            options = coalesce(options, {});

            var helpCons = coalesce(options['help'], this['help']);
            if(helpCons)
                this.help = helpCons;

            this.$el.append(_.template(this.template)(this.templateParams()));

            var buttons = coalesce(
                options['buttons'],
                this['buttons'],
                [ StandardButton.close() ]
                );
            this._buttons = new ModalButtonCollection(buttons);
            this._buttons.each(
                function(button) {
                    this.listenTo(button, 'click', this._end.bind(this, button));
                },
                this
                );

            (new CollectionView({
                model: this._buttons,
                view: ModalButtonView,
                el: this.$('[name=buttons]')
            })).render();

            var cons = coalesce(options['view'], this['view']);
            this.view = new cons({ el: this.contentEl(), model: this['model']});
            this.view.render();
            this.listenTo(this.view, 'finished', this.finish.bind(this));
            $('#modal-container').append(this.el);
        },
        className: 'modal',
        template: '\
<div class="modal-dialog">\
    <div class="modal-content">\
        <%if(help){%>\
            <button type="button" name="help" class="modal-help-button">\
            <span class="oi" data-glyph="question-mark" aria-hidden="true"> </span>\
            Help\
            </button>\
        <%}%>\
        <div name="modal-content"></div>\
        </div>\
    <input type="submit" style="display: none;"></input>\
    <div class="modal-button-box" name="buttons"></div>\
</div>\
            ',
        templateParams: function() {
            return { buttons: this._buttons, help: _.has(this, 'help') };
        },
        contentEl: function() {
            return this.$('*[name=modal-content]')[0];
        },
        events: {
            'click button[name=cancel]': 'remove',
            'click button[name=close]': 'remove',
            'click button[name=help]': function() {
                new Modal({ view: this.help });
            }
        },
        finish: function() {
            this.remove();
        },
        remove: function() {
            Backbone.View.prototype.remove.apply(this, arguments);
            this.trigger('finished');
        },
        _end: function(button) {
            var action = button.get('action');
            console.log('end', action);
            if(_.isFunction(action))
                action.apply(this);
            if(['cancel', 'close'].indexOf(button.get('name')) > -1)
                this.remove();
            this.view.trigger(button.get('name'));
            this.trigger(button.get('name'));
            return false;
        }
    }
    );

var ConfirmModal = Modal.extend(
    {
        initialize: function(options) {
            Modal.prototype.initialize.apply(this, arguments);
            this._callback =
                coalesce(options['callback'], this['callback'], function() {});
            this.view.message = options['message'];
            this.view.render();
            this.on('yes', this.yes.bind(this));
            this.on('no', this.no.bind(this));
        },
        yes: function() {
            this._callback();
            this.remove();
        },
        no: function() {
            this.remove();
        },
        buttons: [ StandardButton.yes(), StandardButton.no() ],
        view: StaticView.extend({
            template: '<p><%-message%>',
            templateParams: function() { return { message: this.message }; },
            message: 'Are you sure?'
        })
    }
    );

